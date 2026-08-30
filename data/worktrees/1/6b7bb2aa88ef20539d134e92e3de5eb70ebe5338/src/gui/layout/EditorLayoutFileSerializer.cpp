#include "gui/layout/EditorLayoutFileSerializer.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace gui {
namespace {
constexpr std::size_t MaximumLayoutNameLength = 256;

void AppendEscapedJsonString(std::string_view value, std::string &output) {
  constexpr char Hex[] = "0123456789abcdef";
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (character < 0x20U) {
        output += "\\u00";
        output.push_back(Hex[(character >> 4U) & 0x0FU]);
        output.push_back(Hex[character & 0x0FU]);
      } else {
        output.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  output.push_back('"');
}

bool IsJsonWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\r'
         || character == '\n';
}

class ExportJsonParser {
public:
  explicit ExportJsonParser(std::string_view input) : input_(input) {}

  bool Parse(EditorLayoutExportData &data, std::string &error) {
    SkipWhitespace();
    if (!Consume('{')) {
      return Fail("JSON root must be an object", error);
    }

    bool hasFormat = false;
    bool hasVersion = false;
    bool hasName = false;
    bool hasLayoutIni = false;
    std::string format;
    int version = 0;
    std::string name;
    std::string layoutIni;

    SkipWhitespace();
    if (!Consume('}')) {
      while (true) {
        std::string key;
        if (!ParseString(key)) {
          return Fail("Expected a JSON object key", error);
        }
        SkipWhitespace();
        if (!Consume(':')) {
          return Fail("Expected ':' after JSON object key", error);
        }
        SkipWhitespace();

        if (key == "format") {
          if (hasFormat || !ParseString(format)) {
            return Fail("format must be a string and appear once", error);
          }
          hasFormat = true;
        } else if (key == "version") {
          if (hasVersion || !ParseInteger(version)) {
            return Fail("version must be an integer and appear once", error);
          }
          hasVersion = true;
        } else if (key == "name") {
          if (hasName || !ParseString(name)) {
            return Fail("name must be a string and appear once", error);
          }
          hasName = true;
        } else if (key == "layout_ini") {
          if (hasLayoutIni || !ParseString(layoutIni)) {
            return Fail("layout_ini must be a string and appear once", error);
          }
          hasLayoutIni = true;
        } else if (!SkipValue()) {
          return Fail("Invalid JSON value", error);
        }

        SkipWhitespace();
        if (Consume('}')) {
          break;
        }
        if (!Consume(',')) {
          return Fail("Expected ',' or '}' in JSON object", error);
        }
        SkipWhitespace();
      }
    }

    SkipWhitespace();
    if (position_ != input_.size()) {
      return Fail("Unexpected content after JSON object", error);
    }
    if (!hasFormat || !hasVersion || !hasName || !hasLayoutIni) {
      return Fail("Missing required layout file field", error);
    }
    if (format != EditorLayoutFileFormat) {
      return Fail("Invalid layout file format", error);
    }
    if (version > EditorLayoutFileVersion) {
      return Fail("Unsupported layout file version", error);
    }
    if (version <= 0) {
      return Fail("Invalid layout file version", error);
    }
    if (name.empty() || name.size() > MaximumLayoutNameLength) {
      return Fail("Layout name must contain 1 to 256 bytes", error);
    }
    if (layoutIni.empty()) {
      return Fail("layout_ini must not be empty", error);
    }

    data = EditorLayoutExportData{
        .version = version,
        .name = std::move(name),
        .imguiIni = std::move(layoutIni),
    };
    error.clear();
    return true;
  }

private:
  void SkipWhitespace() {
    while (position_ < input_.size() && IsJsonWhitespace(input_[position_])) {
      ++position_;
    }
  }

  bool Consume(char expected) {
    if (position_ >= input_.size() || input_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool ParseString(std::string &value) {
    if (!Consume('"')) {
      return false;
    }

    value.clear();
    while (position_ < input_.size()) {
      const unsigned char character = input_[position_++];
      if (character == '"') {
        return true;
      }
      if (character < 0x20U) {
        return false;
      }
      if (character != '\\') {
        value.push_back(static_cast<char>(character));
        continue;
      }
      if (position_ >= input_.size()) {
        return false;
      }

      const char escape = input_[position_++];
      switch (escape) {
      case '"':
      case '\\':
      case '/':
        value.push_back(escape);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      case 'u':
        if (!ParseUnicodeEscape(value)) {
          return false;
        }
        break;
      default:
        return false;
      }
    }
    return false;
  }

  bool ParseUnicodeEscape(std::string &value) {
    std::uint32_t codePoint = 0;
    if (!ParseHexQuad(codePoint)) {
      return false;
    }
    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
      if (position_ + 2 > input_.size() || input_[position_] != '\\'
          || input_[position_ + 1] != 'u') {
        return false;
      }
      position_ += 2;
      std::uint32_t lowSurrogate = 0;
      if (!ParseHexQuad(lowSurrogate) || lowSurrogate < 0xDC00U
          || lowSurrogate > 0xDFFFU) {
        return false;
      }
      codePoint =
          0x10000U + ((codePoint - 0xD800U) << 10U) + (lowSurrogate - 0xDC00U);
    } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
      return false;
    }
    AppendUtf8(codePoint, value);
    return true;
  }

  bool ParseHexQuad(std::uint32_t &value) {
    if (position_ + 4 > input_.size()) {
      return false;
    }
    value = 0;
    for (int index = 0; index < 4; ++index) {
      const char character = input_[position_++];
      value <<= 4U;
      if (character >= '0' && character <= '9') {
        value += static_cast<std::uint32_t>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        value += static_cast<std::uint32_t>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        value += static_cast<std::uint32_t>(character - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  static void AppendUtf8(std::uint32_t codePoint, std::string &value) {
    if (codePoint <= 0x7FU) {
      value.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
      value.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
      value.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
      value.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
      value.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
      value.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
      value.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
      value.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
      value.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
      value.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
  }

  bool ParseInteger(int &value) {
    const std::size_t begin = position_;
    if (position_ < input_.size() && input_[position_] == '-') {
      ++position_;
    }
    const std::size_t digits = position_;
    while (position_ < input_.size() && input_[position_] >= '0'
           && input_[position_] <= '9') {
      ++position_;
    }
    if (digits == position_) {
      position_ = begin;
      return false;
    }
    if ((position_ - digits > 1 && input_[digits] == '0')
        || (position_ < input_.size()
            && (input_[position_] == '.' || input_[position_] == 'e'
                || input_[position_] == 'E'))) {
      position_ = begin;
      return false;
    }

    const std::string_view number = input_.substr(begin, position_ - begin);
    const auto [end, error] =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (error != std::errc{} || end != number.data() + number.size()) {
      position_ = begin;
      return false;
    }
    return true;
  }

  bool SkipValue() {
    SkipWhitespace();
    if (position_ >= input_.size()) {
      return false;
    }
    if (input_[position_] == '"') {
      std::string ignored;
      return ParseString(ignored);
    }
    if (input_[position_] == '{') {
      return SkipObject();
    }
    if (input_[position_] == '[') {
      return SkipArray();
    }
    if (input_[position_] == '-'
        || (input_[position_] >= '0' && input_[position_] <= '9')) {
      return SkipNumber();
    }
    return ConsumeLiteral("true") || ConsumeLiteral("false")
           || ConsumeLiteral("null");
  }

  bool SkipObject() {
    if (!Consume('{')) {
      return false;
    }
    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }
    while (true) {
      std::string key;
      if (!ParseString(key)) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':') || !SkipValue()) {
        return false;
      }
      SkipWhitespace();
      if (Consume('}')) {
        return true;
      }
      if (!Consume(',')) {
        return false;
      }
      SkipWhitespace();
    }
  }

  bool SkipArray() {
    if (!Consume('[')) {
      return false;
    }
    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }
    while (true) {
      if (!SkipValue()) {
        return false;
      }
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        return false;
      }
      SkipWhitespace();
    }
  }

  bool SkipNumber() {
    const std::size_t begin = position_;
    if (input_[position_] == '-') {
      ++position_;
    }
    if (position_ >= input_.size()) {
      position_ = begin;
      return false;
    }
    if (input_[position_] == '0') {
      ++position_;
    } else if (input_[position_] >= '1' && input_[position_] <= '9') {
      while (position_ < input_.size() && input_[position_] >= '0'
             && input_[position_] <= '9') {
        ++position_;
      }
    } else {
      position_ = begin;
      return false;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const std::size_t fraction = position_;
      while (position_ < input_.size() && input_[position_] >= '0'
             && input_[position_] <= '9') {
        ++position_;
      }
      if (fraction == position_) {
        position_ = begin;
        return false;
      }
    }
    if (position_ < input_.size()
        && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size()
          && (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent = position_;
      while (position_ < input_.size() && input_[position_] >= '0'
             && input_[position_] <= '9') {
        ++position_;
      }
      if (exponent == position_) {
        position_ = begin;
        return false;
      }
    }
    return true;
  }

  bool ConsumeLiteral(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  static bool Fail(std::string message, std::string &error) {
    error = std::move(message);
    return false;
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

bool ValidateExportData(const EditorLayoutExportData &data,
    std::string &error) {
  if (data.version != EditorLayoutFileVersion) {
    error = "Unsupported layout file version";
    return false;
  }
  if (data.name.empty() || data.name.size() > MaximumLayoutNameLength) {
    error = "Layout name must contain 1 to 256 bytes";
    return false;
  }
  if (data.imguiIni.empty()) {
    error = "layout_ini must not be empty";
    return false;
  }
  return true;
}
} // namespace

bool EditorLayoutFileSerializer::Serialize(const EditorLayoutExportData &data,
    std::string &json, std::string &error) {
  if (!ValidateExportData(data, error)) {
    return false;
  }

  json = "{\n  \"format\": \"jsb-editor-layout\",\n  \"version\": ";
  json += std::to_string(data.version);
  json += ",\n  \"name\": ";
  AppendEscapedJsonString(data.name, json);
  json += ",\n  \"layout_ini\": ";
  AppendEscapedJsonString(data.imguiIni, json);
  json += "\n}\n";
  if (json.size() > MaximumEditorLayoutFileSize) {
    error = "Layout file exceeds the 4 MiB size limit";
    json.clear();
    return false;
  }
  error.clear();
  return true;
}

bool EditorLayoutFileSerializer::Deserialize(std::string_view json,
    EditorLayoutExportData &data, std::string &error) {
  if (json.size() > MaximumEditorLayoutFileSize) {
    error = "Layout file exceeds the 4 MiB size limit";
    return false;
  }

  EditorLayoutExportData parsed;
  ExportJsonParser parser(json);
  if (!parser.Parse(parsed, error)) {
    return false;
  }
  data = std::move(parsed);
  return true;
}

bool EditorLayoutFileSerializer::Load(const std::filesystem::path &path,
    EditorLayoutExportData &data, std::string &error) {
  std::error_code filesystemError;
  const std::uintmax_t fileSize =
      std::filesystem::file_size(path, filesystemError);
  if (filesystemError) {
    error = "Could not read layout file: " + filesystemError.message();
    return false;
  }
  if (fileSize > MaximumEditorLayoutFileSize) {
    error = "Layout file exceeds the 4 MiB size limit";
    return false;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open layout file";
    return false;
  }
  std::string json(static_cast<std::size_t>(fileSize), '\0');
  if (fileSize > 0) {
    input.read(json.data(), static_cast<std::streamsize>(fileSize));
  }
  if (!input || input.peek() != std::ifstream::traits_type::eof()) {
    error = "Could not read layout file";
    return false;
  }
  return Deserialize(json, data, error);
}

bool EditorLayoutFileSerializer::Save(const std::filesystem::path &path,
    const EditorLayoutExportData &data, std::string &error) {
  std::string json;
  if (!Serialize(data, json, error)) {
    return false;
  }

  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code filesystemError;
    std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) {
      error = "Could not create export directory: " + filesystemError.message();
      return false;
    }
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Could not open layout file for writing";
    return false;
  }
  output.write(json.data(), static_cast<std::streamsize>(json.size()));
  if (!output) {
    error = "Could not write layout file";
    return false;
  }
  error.clear();
  return true;
}
} // namespace gui
