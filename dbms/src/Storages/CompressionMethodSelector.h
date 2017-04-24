#include <IO/CompressedStream.h>
#include <IO/ReadHelpers.h>
#include <Common/Exception.h>
#include <Common/StringUtils.h>
#include <Poco/Util/AbstractConfiguration.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int UNKNOWN_COMPRESSION_METHOD;
    extern const int UNKNOWN_ELEMENT_IN_CONFIG;
}


/** Allows you to select the compression method for the conditions specified in the configuration file.
  * The config looks like this

    <compression>

        <!-- Set of options. Options are checked in a row. The last worked option wins. If none has worked, then lz4 is used. -->
        <case>

            <!-- Conditions. All must be satisfied simultaneously. Some conditions may not be specified. -->
            <min_part_size>10000000000</min_part_size>         <!-- The minimum size of a part in bytes. -->
            <min_part_size_ratio>0.01</min_part_size_ratio>    <!-- The minimum size of the part relative to all the data in the table. -->

            <! - Which compression method to choose. ->
            <method>zstd</method>
        </case>

        <case>
                ...
        </case>
    </compression>
  */
class CompressionMethodSelector
{
private:
    struct Element
    {
        size_t min_part_size = 0;
        double min_part_size_ratio = 0;
        CompressionMethod method = CompressionMethod::LZ4;

        void setMethod(const std::string & name)
        {
            if (name == "lz4")
                method = CompressionMethod::LZ4;
            else if (name == "zstd")
                method = CompressionMethod::ZSTD;
            else
                throw Exception("Unknown compression method " + name, ErrorCodes::UNKNOWN_COMPRESSION_METHOD);
        }

        Element(Poco::Util::AbstractConfiguration & config, const std::string & config_prefix)
        {
            min_part_size = config.getUInt64(config_prefix + ".min_part_size", 0);
            min_part_size_ratio = config.getDouble(config_prefix + ".min_part_size_ratio", 0);

            setMethod(config.getString(config_prefix + ".method"));
        }

        bool check(size_t part_size, double part_size_ratio) const
        {
            return part_size >= min_part_size
                && part_size_ratio >= min_part_size_ratio;
        }
    };

    std::vector<Element> elements;

public:
    CompressionMethodSelector() {}    /// Always returns the default method.

    CompressionMethodSelector(Poco::Util::AbstractConfiguration & config, const std::string & config_prefix)
    {
        Poco::Util::AbstractConfiguration::Keys keys;
        config.keys(config_prefix, keys);

        for (const auto & name : keys)
        {
            if (!startsWith(name.data(), "case"))
                throw Exception("Unknown element in config: " + config_prefix + "." + name + ", must be 'case'", ErrorCodes::UNKNOWN_ELEMENT_IN_CONFIG);

            elements.emplace_back(config, config_prefix + "." + name);
        }
    }

    CompressionMethod choose(size_t part_size, double part_size_ratio) const
    {
        CompressionMethod res = CompressionMethod::LZ4;

        for (const auto & element : elements)
            if (element.check(part_size, part_size_ratio))
                res = element.method;

        return res;
    }
};

}
