#include "extension_api.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size]) { return {text, Size - 1}; }

NsStringView view(std::string_view text) { return {text.data(), text.size()}; }

const NsHostApi *hostApi = nullptr;
NsExtensionHandle extensionHandle = nullptr;

std::string setting(std::string_view key, std::string_view fallback)
{
    if (!hostApi || !hostApi->get_extension_setting || !extensionHandle)
        return std::string(fallback);
    const size_t required = hostApi->get_extension_setting(
        hostApi->host_context, extensionHandle, view(key), nullptr, 0);
    if (required == 0) return std::string(fallback);
    std::string result(required, '\0');
    hostApi->get_extension_setting(hostApi->host_context, extensionHandle, view(key),
                                   result.data(), result.size());
    return result;
}

bool booleanSetting(std::string_view key, bool fallback)
{
    std::string value = setting(key, fallback ? "true" : "false");
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

int integerSetting(std::string_view key, int fallback, int minimum, int maximum)
{
    try {
        return std::clamp(std::stoi(setting(key, std::to_string(fallback))), minimum, maximum);
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> splitLines(std::string_view source)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < source.size()) {
        size_t end = start;
        while (end < source.size() && source[end] != '\n' && source[end] != '\r') ++end;
        lines.emplace_back(source.substr(start, end - start));
        if (end < source.size() && source[end] == '\r' && end + 1 < source.size()
            && source[end + 1] == '\n') start = end + 2;
        else start = end + 1;
    }
    return lines;
}

void trimRight(std::string &text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
}

std::string normalizeLeadingWhitespace(std::string_view line, int indentWidth)
{
    std::string result;
    size_t column = 0;
    size_t index = 0;
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) {
        if (line[index] == '\t') {
            const size_t spaces = static_cast<size_t>(indentWidth)
                - (column % static_cast<size_t>(indentWidth));
            result.append(spaces, ' ');
            column += spaces;
        } else {
            result.push_back(' ');
            ++column;
        }
        ++index;
    }
    result.append(line.substr(index));
    trimRight(result);
    return result;
}

enum class TokenKind { Word, String, Comment, Operator, Open, Close, Comma, Colon, Dot, Semicolon };
struct Token { TokenKind kind; std::string text; };

bool isIdentifierStart(unsigned char character)
{
    return std::isalpha(character) || character == '_';
}

bool isIdentifierPart(unsigned char character)
{
    return std::isalnum(character) || character == '_';
}

bool isStringPrefix(std::string_view word)
{
    std::string lowered(word);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == "r" || lowered == "u" || lowered == "b" || lowered == "f"
        || lowered == "br" || lowered == "rb" || lowered == "fr" || lowered == "rf";
}

size_t scanQuoted(std::string_view source, size_t quoteIndex)
{
    const char quote = source[quoteIndex];
    const bool triple = quoteIndex + 2 < source.size()
        && source[quoteIndex + 1] == quote && source[quoteIndex + 2] == quote;
    size_t index = quoteIndex + (triple ? 3 : 1);
    bool escaped = false;
    while (index < source.size()) {
        if (escaped) { escaped = false; ++index; continue; }
        if (source[index] == '\\') { escaped = true; ++index; continue; }
        if (triple && index + 2 < source.size() && source[index] == quote
            && source[index + 1] == quote && source[index + 2] == quote) return index + 3;
        if (!triple && source[index] == quote) return index + 1;
        ++index;
    }
    return source.size();
}

std::vector<Token> pythonTokens(std::string_view source)
{
    std::vector<Token> tokens;
    size_t index = 0;
    while (index < source.size()) {
        const unsigned char current = static_cast<unsigned char>(source[index]);
        if (std::isspace(current)) { ++index; continue; }
        if (source[index] == '#') {
            tokens.push_back({TokenKind::Comment, std::string(source.substr(index))});
            break;
        }
        if (source[index] == '\'' || source[index] == '"') {
            const size_t end = scanQuoted(source, index);
            tokens.push_back({TokenKind::String, std::string(source.substr(index, end - index))});
            index = end;
            continue;
        }
        if (isIdentifierStart(current)) {
            const size_t start = index++;
            while (index < source.size()
                   && isIdentifierPart(static_cast<unsigned char>(source[index]))) ++index;
            const std::string_view word = source.substr(start, index - start);
            if (index < source.size() && (source[index] == '\'' || source[index] == '"')
                && isStringPrefix(word)) {
                const size_t end = scanQuoted(source, index);
                tokens.push_back({TokenKind::String, std::string(source.substr(start, end - start))});
                index = end;
            } else tokens.push_back({TokenKind::Word, std::string(word)});
            continue;
        }
        if (std::isdigit(current)) {
            const size_t start = index++;
            while (index < source.size()) {
                const unsigned char character = static_cast<unsigned char>(source[index]);
                if (!std::isalnum(character) && source[index] != '_' && source[index] != '.') break;
                ++index;
            }
            tokens.push_back({TokenKind::Word, std::string(source.substr(start, index - start))});
            continue;
        }
        const char character = source[index];
        if (character == '(' || character == '[' || character == '{') {
            tokens.push_back({TokenKind::Open, std::string(1, character)}); ++index; continue;
        }
        if (character == ')' || character == ']' || character == '}') {
            tokens.push_back({TokenKind::Close, std::string(1, character)}); ++index; continue;
        }
        if (character == ',') { tokens.push_back({TokenKind::Comma, ","}); ++index; continue; }
        if (character == ':') { tokens.push_back({TokenKind::Colon, ":"}); ++index; continue; }
        if (character == '.') { tokens.push_back({TokenKind::Dot, "."}); ++index; continue; }
        if (character == ';') { tokens.push_back({TokenKind::Semicolon, ";"}); ++index; continue; }
        static constexpr std::string_view operators[] = {
            "**=", "//=", "<<=", ">>=", "==", "!=", "<=", ">=", "+=", "-=", "*=",
            "/=", "%=", "&=", "|=", "^=", "->", ":=", "**", "//", "<<", ">>"
        };
        bool matched = false;
        for (const auto op : operators) {
            if (source.substr(index).starts_with(op)) {
                tokens.push_back({TokenKind::Operator, std::string(op)});
                index += op.size(); matched = true; break;
            }
        }
        if (!matched) { tokens.push_back({TokenKind::Operator, std::string(1, character)}); ++index; }
    }
    return tokens;
}

void removeTrailingSpaces(std::string &text)
{
    while (!text.empty() && text.back() == ' ') text.pop_back();
}

void appendSpace(std::string &text)
{
    if (!text.empty() && text.back() != ' ') text.push_back(' ');
}

bool isControlKeyword(std::string_view word)
{
    return word == "if" || word == "elif" || word == "for" || word == "while"
        || word == "with" || word == "except" || word == "assert" || word == "match"
        || word == "case";
}

bool isExpressionKeyword(std::string_view word)
{
    return isControlKeyword(word) || word == "return" || word == "yield"
        || word == "assert" || word == "raise" || word == "del";
}

std::string formatPythonSpacing(std::string_view source)
{
    const auto tokens = pythonTokens(source);
    std::string result;
    struct Delimiter { char opening; bool annotation = false; };
    std::vector<Delimiter> delimiters;
    int parenDepth = 0;
    for (size_t index = 0; index < tokens.size(); ++index) {
        const Token &token = tokens[index];
        const Token *previous = index > 0 ? &tokens[index - 1] : nullptr;
        const Token *next = index + 1 < tokens.size() ? &tokens[index + 1] : nullptr;
        if (token.kind == TokenKind::Comment) {
            removeTrailingSpaces(result);
            if (!result.empty()) result += "  ";
            result += token.text;
        } else if (token.kind == TokenKind::Comma) {
            removeTrailingSpaces(result); result += ',';
            if (!delimiters.empty() && delimiters.back().opening == '(')
                delimiters.back().annotation = false;
            if (next && next->kind != TokenKind::Close) result += ' ';
        } else if (token.kind == TokenKind::Colon) {
            removeTrailingSpaces(result); result += ':';
            const bool slice = !delimiters.empty() && delimiters.back().opening == '[';
            if (!delimiters.empty() && delimiters.back().opening == '(')
                delimiters.back().annotation = true;
            if (!slice && next && next->kind != TokenKind::Close
                && next->kind != TokenKind::Comma && next->kind != TokenKind::Comment) result += ' ';
        } else if (token.kind == TokenKind::Semicolon) {
            removeTrailingSpaces(result); result += ';'; if (next) result += ' ';
        } else if (token.kind == TokenKind::Dot) {
            if (previous && previous->kind == TokenKind::Word && previous->text == "from")
                appendSpace(result);
            else removeTrailingSpaces(result);
            result += '.';
        } else if (token.kind == TokenKind::Open) {
            const char opening = token.text.front();
            if (opening == '(' && previous && previous->kind == TokenKind::Word
                && isControlKeyword(previous->text)) appendSpace(result);
            else if (previous && previous->kind == TokenKind::Word
                     && isExpressionKeyword(previous->text)) appendSpace(result);
            else if (opening == '{' && previous
                     && (previous->kind == TokenKind::Word || previous->kind == TokenKind::Close))
                appendSpace(result);
            else removeTrailingSpaces(result);
            result += opening;
            delimiters.push_back({opening, false});
            if (opening == '(') ++parenDepth;
        } else if (token.kind == TokenKind::Close) {
            removeTrailingSpaces(result); result += token.text;
            if (token.text == ")" && parenDepth > 0) --parenDepth;
            if (!delimiters.empty()) delimiters.pop_back();
        } else if (token.kind == TokenKind::Operator) {
            const bool unary = (token.text == "+" || token.text == "-" || token.text == "~"
                                || token.text == "*" || token.text == "**" || token.text == "@")
                && (!previous || previous->kind == TokenKind::Operator
                    || previous->kind == TokenKind::Open || previous->kind == TokenKind::Comma
                    || previous->kind == TokenKind::Colon);
            const bool compactDefault = token.text == "=" && parenDepth > 0
                && !delimiters.empty() && delimiters.back().opening == '('
                && !delimiters.back().annotation;
            if (unary || compactDefault) {
                removeTrailingSpaces(result); result += token.text;
            } else {
                removeTrailingSpaces(result); appendSpace(result); result += token.text;
                if (next) result += ' ';
            }
        } else {
            if (previous && (previous->kind == TokenKind::Word
                             || previous->kind == TokenKind::String
                             || previous->kind == TokenKind::Close
                             || (previous->kind == TokenKind::Dot && token.text == "import")))
                appendSpace(result);
            result += token.text;
        }
    }
    removeTrailingSpaces(result);
    return result;
}

size_t tripleQuoteCount(std::string_view text)
{
    size_t count = 0;
    for (size_t index = 0; index + 2 < text.size(); ++index) {
        if (text.substr(index, 3) == "\"\"\"" || text.substr(index, 3) == "'''") {
            ++count; index += 2;
        }
    }
    return count;
}

bool startsWithDefinition(std::string_view text, std::string_view keyword)
{
    return text.starts_with(keyword) && text.size() > keyword.size()
        && std::isspace(static_cast<unsigned char>(text[keyword.size()]));
}

bool previousLineIsDecorator(const std::vector<std::string> &lines)
{
    if (lines.empty()) return false;
    const std::string &line = lines.back();
    const size_t first = line.find_first_not_of(' ');
    return first != std::string::npos && line[first] == '@';
}

std::string formatPython(std::string_view source)
{
    const int indentWidth = integerSetting("python_indent_width", 4, 2, 8);
    const bool normalizeSpacing = booleanSetting("python_normalize_spacing", true);
    const bool normalizeBlankLines = booleanSetting("python_blank_lines", true);
    const auto input = splitLines(source);
    std::vector<std::string> output;
    std::vector<size_t> indentStack{0};
    bool inTripleString = false;
    for (const std::string &rawLine : input) {
        if (inTripleString) {
            output.push_back(rawLine);
            if (tripleQuoteCount(rawLine) % 2 == 1) inTripleString = false;
            continue;
        }
        std::string normalized = normalizeLeadingWhitespace(rawLine, indentWidth);
        const size_t first = normalized.find_first_not_of(' ');
        if (first == std::string::npos) {
            if (output.empty() || !output.back().empty()) output.emplace_back();
            continue;
        }
        const size_t rawIndent = first;
        while (indentStack.size() > 1 && rawIndent < indentStack.back()) indentStack.pop_back();
        if (rawIndent > indentStack.back()) indentStack.push_back(rawIndent);
        const size_t level = indentStack.size() - 1;
        std::string content = normalized.substr(first);
        if (!output.empty() && output.back().empty()) {
            size_t previous = output.size();
            while (previous > 0 && output[previous - 1].empty()) --previous;
            if (previous > 0) {
                const std::string &header = output[previous - 1];
                const size_t headerFirst = header.find_first_not_of(' ');
                const size_t headerIndent = headerFirst == std::string::npos ? 0 : headerFirst;
                const size_t headerLevel = headerIndent / static_cast<size_t>(indentWidth);
                if (!header.empty() && header.back() == ':' && level > headerLevel)
                    output.erase(output.begin() + static_cast<std::ptrdiff_t>(previous), output.end());
            }
        }
        const bool hasTripleQuote = tripleQuoteCount(content) % 2 == 1;
        if (normalizeSpacing && !inTripleString && !hasTripleQuote)
            content = formatPythonSpacing(content);
        if (normalizeBlankLines && !inTripleString) {
            size_t required = 0;
            const bool definition = startsWithDefinition(content, "def")
                || startsWithDefinition(content, "class")
                || startsWithDefinition(content, "async def");
            const bool decorator = content.starts_with('@');
            if (level == 0 && (definition || decorator))
                required = output.empty() || (definition && previousLineIsDecorator(output)) ? 0 : 2;
            else if (level == 1 && (startsWithDefinition(content, "def")
                                    || startsWithDefinition(content, "async def") || decorator))
                required = output.empty() || (definition && previousLineIsDecorator(output)) ? 0 : 1;
            size_t existing = 0;
            for (auto it = output.rbegin(); it != output.rend() && it->empty(); ++it) ++existing;
            while (existing < required) { output.emplace_back(); ++existing; }
        }
        output.push_back(std::string(level * static_cast<size_t>(indentWidth), ' ') + content);
        if (hasTripleQuote) inTripleString = !inTripleString;
    }
    while (!output.empty() && output.back().empty()) output.pop_back();
    if (output.empty()) return {};
    std::string result;
    for (const auto &line : output) { result += line; result.push_back('\n'); }
    return result;
}

std::string tidyWhitespace(std::string_view source)
{
    std::vector<std::string> lines = splitLines(source);
    for (auto &line : lines) line = normalizeLeadingWhitespace(line, 4);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.empty()) return {};
    std::string result;
    for (const auto &line : lines) { result += line; result.push_back('\n'); }
    return result;
}

bool isPythonPath(std::string_view path)
{
    std::string lowered(path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered.ends_with(".py") || lowered.ends_with(".pyw") || lowered.ends_with(".pyi");
}

bool replaceIfChanged(std::string_view source, std::string_view path)
{
    if (!hostApi || !hostApi->replace_document_text) return false;
    const std::string formatted = isPythonPath(path) ? formatPython(source) : tidyWhitespace(source);
    if (formatted == source) return false;
    return hostApi->replace_document_text(hostApi->host_context,
        {formatted.data(), formatted.size()}) != 0;
}

void formatDocument(void *, const NsCommandContext *context)
{
    if (!hostApi || !hostApi->get_active_document_text) return;
    const size_t required = hostApi->get_active_document_text(hostApi->host_context, nullptr, 0);
    std::string source(required, '\0');
    if (required > 0)
        hostApi->get_active_document_text(hostApi->host_context, source.data(), source.size());
    const std::string_view path = context && context->active_file_path.data
        ? std::string_view(context->active_file_path.data, context->active_file_path.size)
        : std::string_view{};
    if (replaceIfChanged(source, path)) {
        hostApi->show_message(hostApi->host_context,
            isPythonPath(path) ? view("Code Tidy applied Python formatting.")
                               : view("Code Tidy cleaned document whitespace."), 2500);
    } else hostApi->show_message(hostApi->host_context, view("Code Tidy: already clean."), 2000);
}

void onHostEvent(void *, const NsEventContext *context)
{
    if (!context || context->struct_size < sizeof(NsEventContext)
        || context->event_type != NS_EVENT_DOCUMENT_WILL_SAVE
        || !booleanSetting("format_on_save", true)) return;
    const std::string_view source(context->document_text.data ? context->document_text.data : "",
        context->document_text.data ? context->document_text.size : 0);
    const std::string_view path(context->active_file_path.data ? context->active_file_path.data : "",
        context->active_file_path.data ? context->active_file_path.size : 0);
    if (replaceIfChanged(source, path) && hostApi && hostApi->show_message)
        hostApi->show_message(hostApi->host_context, view("Code Tidy formatted on save."), 1500);
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, get_extension_setting)
        + sizeof(host->get_extension_setting);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command
        || !host->register_event_listener || !host->get_active_document_text
        || !host->register_settings_page || !host->get_extension_setting) return -1;
    hostApi = host;
    extensionHandle = extension;
    const NsCommandSpec command{
        sizeof(NsCommandSpec), view("dev.nativestudio.code_tidy.format_document"),
        view("Format Document"), view("Code Tidy"), view("Ctrl+Shift+I"), &formatDocument, nullptr
    };
    const NsSettingSpec settings[]{
        {sizeof(NsSettingSpec), view("format_on_save"), view("Format on save"),
         view("Run Code Tidy immediately before writing a document."), NS_SETTING_BOOLEAN,
         view("true"), 0, 0, nullptr, 0},
        {sizeof(NsSettingSpec), view("python_normalize_spacing"), view("Normalize Python spacing"),
         view("Apply PEP 8-style spacing around operators, commas, colons, and delimiters."),
         NS_SETTING_BOOLEAN, view("true"), 0, 0, nullptr, 0},
        {sizeof(NsSettingSpec), view("python_blank_lines"), view("Normalize Python blank lines"),
         view("Use two blank lines before top-level definitions and one before methods."),
         NS_SETTING_BOOLEAN, view("true"), 0, 0, nullptr, 0},
        {sizeof(NsSettingSpec), view("python_indent_width"), view("Python indentation width"),
         view("PEP 8 uses four spaces."), NS_SETTING_INTEGER,
         view("4"), 2, 8, nullptr, 0}
    };
    const NsSettingsPageSpec settingsPage{
        sizeof(NsSettingsPageSpec), view("Code Tidy"),
        view("Language-specific formatting provided by the bundled Code Tidy extension."),
        view("formatter format python pep8 whitespace save indentation"),
        settings, sizeof(settings) / sizeof(settings[0])
    };
    if (!host->register_command(host->host_context, extension, &command)
        || !host->register_event_listener(host->host_context, extension,
                                          NS_EVENT_DOCUMENT_WILL_SAVE, &onHostEvent, nullptr)
        || !host->register_settings_page(host->host_context, extension, &settingsPage)) return -1;
    host->log(host->host_context, NS_LOG_INFO, view("Activated bundled Code Tidy extension"));
    return 0;
}

void deactivate(void *) { extensionHandle = nullptr; hostApi = nullptr; }

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor), NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.code_tidy"), view("Code Tidy"), view("1.2.0"),
    nullptr, &activate, &deactivate
};
}

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void) { return &descriptor; }
