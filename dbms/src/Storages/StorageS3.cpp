#include <Common/config.h>

#if USE_AWS_S3

#include <IO/S3Common.h>
#include <Storages/StorageFactory.h>
#include <Storages/StorageS3.h>

#include <Interpreters/Context.h>
#include <Interpreters/evaluateConstantExpression.h>
#include <Parsers/ASTLiteral.h>

#include <IO/ReadBufferFromS3.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteBufferFromS3.h>
#include <IO/WriteHelpers.h>

#include <Formats/FormatFactory.h>

#include <DataStreams/IBlockOutputStream.h>
#include <DataStreams/IBlockInputStream.h>
#include <DataStreams/AddingDefaultsBlockInputStream.h>
#include <DataStreams/narrowBlockInputStreams.h>

#include <DataTypes/DataTypeString.h>

#include <aws/s3/S3Client.h>
#include <aws/s3/model/ListObjectsRequest.h>

#include <Common/parseGlobs.h>
#include <re2/re2.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int UNEXPECTED_EXPRESSION;
    extern const int S3_ERROR;
}


namespace
{
    class StorageS3BlockInputStream : public IBlockInputStream
    {
    public:
        StorageS3BlockInputStream(
            bool need_path,
            bool need_file,
            const String & format,
            const String & name_,
            const Block & sample_block,
            const Context & context,
            UInt64 max_block_size,
            const CompressionMethod compression_method,
            const std::shared_ptr<Aws::S3::S3Client> & client,
            const String & bucket,
            const String & key)
            : name(name_)
            , with_file_column(need_file)
            , with_path_column(need_path)
            , file_path(bucket + "/" + key)
        {
            read_buf = wrapReadBufferWithCompressionMethod(std::make_unique<ReadBufferFromS3>(client, bucket, key), compression_method);
            reader = FormatFactory::instance().getInput(format, *read_buf, sample_block, context, max_block_size);
        }

        String getName() const override
        {
            return name;
        }

        Block readImpl() override
        {
            auto res = reader->read();
            if (res)
            {
                if (with_path_column)
                    res.insert({DataTypeString().createColumnConst(res.rows(), file_path)->convertToFullColumnIfConst(), std::make_shared<DataTypeString>(),
                            "_path"});  /// construction with const is for probably generating less code
                if (with_file_column)
                {
                    size_t last_slash_pos = file_path.find_last_of('/');
                    res.insert({DataTypeString().createColumnConst(res.rows(), file_path.substr(
                            last_slash_pos + 1))->convertToFullColumnIfConst(), std::make_shared<DataTypeString>(),
                                "_file"});
                }
            }
            return res;
        }

        Block getHeader() const override
        {
            auto res = reader->getHeader();
            if (res)
            {
                if (with_path_column)
                    res.insert({DataTypeString().createColumn(), std::make_shared<DataTypeString>(), "_path"});
                if (with_file_column)
                    res.insert({DataTypeString().createColumn(), std::make_shared<DataTypeString>(), "_file"});
            }
            return res;
        }

        void readPrefixImpl() override
        {
            reader->readPrefix();
        }

        void readSuffixImpl() override
        {
            reader->readSuffix();
        }

    private:
        String name;
        std::unique_ptr<ReadBuffer> read_buf;
        BlockInputStreamPtr reader;
        bool with_file_column = false;
        bool with_path_column = false;
        String file_path;
    };

    class StorageS3BlockOutputStream : public IBlockOutputStream
    {
    public:
        StorageS3BlockOutputStream(
            const String & format,
            UInt64 min_upload_part_size,
            const Block & sample_block_,
            const Context & context,
            const CompressionMethod compression_method,
            const std::shared_ptr<Aws::S3::S3Client> & client,
            const String & bucket,
            const String & key)
            : sample_block(sample_block_)
        {
            write_buf = wrapWriteBufferWithCompressionMethod(
                std::make_unique<WriteBufferFromS3>(client, bucket, key, min_upload_part_size), compression_method, 3);
            writer = FormatFactory::instance().getOutput(format, *write_buf, sample_block, context);
        }

        Block getHeader() const override
        {
            return sample_block;
        }

        void write(const Block & block) override
        {
            writer->write(block);
        }

        void writePrefix() override
        {
            writer->writePrefix();
        }

        void writeSuffix() override
        {
            writer->writeSuffix();
            writer->flush();
            write_buf->finalize();
        }

    private:
        Block sample_block;
        std::unique_ptr<WriteBuffer> write_buf;
        BlockOutputStreamPtr writer;
    };
}


StorageS3::StorageS3(
    const S3::URI & uri_,
    const String & access_key_id_,
    const String & secret_access_key_,
    const StorageID & table_id_,
    const String & format_name_,
    UInt64 min_upload_part_size_,
    const ColumnsDescription & columns_,
    const ConstraintsDescription & constraints_,
    Context & context_,
    const String & compression_method_ = "")
    : IStorage(table_id_, ColumnsDescription({
            {"_path", std::make_shared<DataTypeString>()},
            {"_file", std::make_shared<DataTypeString>()}
        }, true))
    , uri(uri_)
    , context_global(context_)
    , format_name(format_name_)
    , min_upload_part_size(min_upload_part_size_)
    , compression_method(compression_method_)
    , client(S3::ClientFactory::instance().create(uri_.endpoint, access_key_id_, secret_access_key_))
{
    context_global.getRemoteHostFilter().checkURL(uri_.uri);
    setColumns(columns_);
    setConstraints(constraints_);
}


namespace
{

/* "Recursive" directory listing with matched paths as a result.
 * Have the same method in StorageFile.
 */
Strings listFilesWithRegexpMatching(Aws::S3::S3Client & client, const S3::URI & globbed_uri)
{
    if (globbed_uri.bucket.find_first_of("*?{") != globbed_uri.bucket.npos)
    {
        throw Exception("Expression can not have wildcards inside bucket name", ErrorCodes::UNEXPECTED_EXPRESSION);
    }

    const String key_prefix = globbed_uri.key.substr(0, globbed_uri.key.find_first_of("*?{"));
    if (key_prefix.size() == globbed_uri.key.size())
    {
        return {globbed_uri.key};
    }

    Aws::S3::Model::ListObjectsRequest request;
    request.SetBucket(globbed_uri.bucket);
    request.SetPrefix(key_prefix);

    re2::RE2 matcher(makeRegexpPatternFromGlobs(globbed_uri.key));
    Strings result;
    Aws::S3::Model::ListObjectsOutcome outcome;
    int page = 0;
    do
    {
        ++page;
        outcome = client.ListObjects(request);
        if (!outcome.IsSuccess())
        {
            throw Exception("Could not list objects in bucket " + quoteString(request.GetBucket())
                    + " with prefix " + quoteString(request.GetPrefix())
                    + ", page " + std::to_string(page), ErrorCodes::S3_ERROR);
        }

        for (const auto & row : outcome.GetResult().GetContents())
        {
            String key = row.GetKey();
            if (re2::RE2::FullMatch(key, matcher))
                result.emplace_back(std::move(key));
        }

        request.SetMarker(outcome.GetResult().GetNextMarker());
    }
    while (outcome.GetResult().GetIsTruncated());

    return result;
}

}


BlockInputStreams StorageS3::read(
    const Names & column_names,
    const SelectQueryInfo & /*query_info*/,
    const Context & context,
    QueryProcessingStage::Enum /*processed_stage*/,
    size_t max_block_size,
    unsigned num_streams)
{
    BlockInputStreams result;
    bool need_path_column = false;
    bool need_file_column = false;
    for (const auto & column : column_names)
    {
        if (column == "_path")
            need_path_column = true;
        if (column == "_file")
            need_file_column = true;
    }

    for (const String & key : listFilesWithRegexpMatching(*client, uri))
    {
        BlockInputStreamPtr block_input = std::make_shared<StorageS3BlockInputStream>(
            need_path_column,
            need_file_column,
            format_name,
            getName(),
            getHeaderBlock(column_names),
            context,
            max_block_size,
            chooseCompressionMethod(uri.endpoint, compression_method),
            client,
            uri.bucket,
            key);

        auto column_defaults = getColumns().getDefaults();
        if (column_defaults.empty())
            result.emplace_back(std::move(block_input));
        else
            result.emplace_back(std::make_shared<AddingDefaultsBlockInputStream>(block_input, column_defaults, context));
    }

    return narrowBlockInputStreams(result, num_streams);
}

BlockOutputStreamPtr StorageS3::write(const ASTPtr & /*query*/, const Context & /*context*/)
{
    return std::make_shared<StorageS3BlockOutputStream>(
        format_name, min_upload_part_size, getSampleBlock(), context_global,
        chooseCompressionMethod(uri.endpoint, compression_method),
        client, uri.bucket, uri.key);
}

void registerStorageS3(StorageFactory & factory)
{
    factory.registerStorage("S3", [](const StorageFactory::Arguments & args)
    {
        ASTs & engine_args = args.engine_args;

        if (engine_args.size() < 2 || engine_args.size() > 5)
            throw Exception(
                "Storage S3 requires 2 to 5 arguments: url, [access_key_id, secret_access_key], name of used format and [compression_method].", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        for (size_t i = 0; i < engine_args.size(); ++i)
            engine_args[i] = evaluateConstantExpressionOrIdentifierAsLiteral(engine_args[i], args.local_context);

        String url = engine_args[0]->as<ASTLiteral &>().value.safeGet<String>();
        Poco::URI uri (url);
        S3::URI s3_uri (uri);

        String format_name = engine_args[engine_args.size() - 1]->as<ASTLiteral &>().value.safeGet<String>();

        String access_key_id;
        String secret_access_key;
        if (engine_args.size() >= 4)
        {
            access_key_id = engine_args[1]->as<ASTLiteral &>().value.safeGet<String>();
            secret_access_key = engine_args[2]->as<ASTLiteral &>().value.safeGet<String>();
        }

        UInt64 min_upload_part_size = args.local_context.getSettingsRef().s3_min_upload_part_size;

        String compression_method;
        if (engine_args.size() == 3 || engine_args.size() == 5)
            compression_method = engine_args.back()->as<ASTLiteral &>().value.safeGet<String>();
        else
            compression_method = "auto";

        return StorageS3::create(s3_uri, access_key_id, secret_access_key, args.table_id, format_name, min_upload_part_size, args.columns, args.constraints, args.context);
    });
}

}

#endif
