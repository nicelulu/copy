#pragma once

#include "HTTPHandlerFactory.h"

#include <re2/re2.h>
#include <re2/stringpiece.h>
#include <Poco/StringTokenizer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Util/LayeredConfiguration.h>

#include <common/find_symbols.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int CANNOT_COMPILE_REGEXP;
    extern const int UNKNOWN_ELEMENT_IN_CONFIG;
}


typedef std::shared_ptr<const re2::RE2> CompiledRegexPtr;

static inline bool checkRegexExpression(const StringRef & match_str, const CompiledRegexPtr & compiled_regex)
{
    int num_captures = compiled_regex->NumberOfCapturingGroups() + 1;

    re2::StringPiece matches[num_captures];
    re2::StringPiece match_input(match_str.data, match_str.size);
    return compiled_regex->Match(match_input, 0, match_str.size, re2::RE2::Anchor::ANCHOR_BOTH, matches, num_captures);
}

static inline bool checkExpression(const StringRef & match_str, const std::pair<String, CompiledRegexPtr> & expression)
{
    if (expression.second)
        return checkRegexExpression(match_str, expression.second);

    return match_str == expression.first;
}

static inline auto methodsFilter(Poco::Util::AbstractConfiguration & config, const std::string & config_path)
{
    std::vector<String> methods;
    Poco::StringTokenizer tokenizer(config.getString(config_path), ",");

    for (auto iterator = tokenizer.begin(); iterator != tokenizer.end(); ++iterator)
        methods.emplace_back(Poco::toUpper(Poco::trim(*iterator)));

    return [methods](const Poco::Net::HTTPServerRequest & request) { return std::count(methods.begin(), methods.end(), request.getMethod()); };
}

static inline auto getExpression(const std::string & expression)
{
    if (!startsWith(expression, "regex:"))
        return std::make_pair(expression, CompiledRegexPtr{});

    auto compiled_regex = std::make_shared<const re2::RE2>(expression.substr(6));

    if (!compiled_regex->ok())
        throw Exception("cannot compile re2: " + expression + " for http handling rule, error: " + compiled_regex->error() +
                        ". Look at https://github.com/google/re2/wiki/Syntax for reference.", ErrorCodes::CANNOT_COMPILE_REGEXP);
    return std::make_pair(expression, compiled_regex);
}

static inline auto urlFilter(Poco::Util::AbstractConfiguration & config, const std::string & config_path)
{
    return [expression = getExpression(config.getString(config_path))](const Poco::Net::HTTPServerRequest & request)
    {
        const auto & uri = request.getURI();
        const auto & end = find_first_symbols<'?'>(uri.data(), uri.data() + uri.size());

        return checkExpression(StringRef(uri.data(), end - uri.data()), expression);
    };
}

static inline auto headersFilter(Poco::Util::AbstractConfiguration & config, const std::string & prefix)
{
    std::unordered_map<String, std::pair<String, CompiledRegexPtr>> headers_expression;
    Poco::Util::AbstractConfiguration::Keys headers_name;
    config.keys(prefix, headers_name);

    for (const auto & header_name : headers_name)
    {
        const auto & expression = getExpression(config.getString(prefix + "." + header_name));
        checkExpression("", expression);    /// Check expression syntax is correct
        headers_expression.emplace(std::make_pair(header_name, expression));
    }

    return [headers_expression](const Poco::Net::HTTPServerRequest & request)
    {
        for (const auto & [header_name, header_expression] : headers_expression)
        {
            const auto & header_value = request.get(header_name, "");
            if (!checkExpression(StringRef(header_value.data(), header_value.size()), header_expression))
                return false;
        }

        return true;
    };
}

template <typename TEndpoint>
static inline Poco::Net::HTTPRequestHandlerFactory * addFiltersFromConfig(
    HandlingRuleHTTPHandlerFactory <TEndpoint> * factory, Poco::Util::AbstractConfiguration & config, const std::string & prefix)
{
    Poco::Util::AbstractConfiguration::Keys filters_type;
    config.keys(prefix, filters_type);

    for (const auto & filter_type : filters_type)
    {
        if (filter_type == "handler")
            continue;
        else if (filter_type == "url")
            factory->addFilter(urlFilter(config, prefix + ".url"));
        else if (filter_type == "headers")
            factory->addFilter(headersFilter(config, prefix + ".headers"));
        else if (filter_type == "methods")
            factory->addFilter(methodsFilter(config, prefix + ".methods"));
        else
            throw Exception("Unknown element in config: " + prefix + "." + filter_type, ErrorCodes::UNKNOWN_ELEMENT_IN_CONFIG);
    }

    return factory;
}

}
