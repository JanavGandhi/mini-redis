// resp.cpp — Implementation of RESP2 parser and encoder.

#include "resp.h"

#include <charconv>    // std::from_chars (fast, non-throwing integer parsing)
#include <string_view> // std::string_view

// ─── Internal Parsing Helpers ───────────────────────────────────────────────

namespace {

// Find the position of CRLF ("\r\n") in a string_view.
// Returns std::string_view::npos if not found.
std::size_t find_crlf(std::string_view sv) {
    for (std::size_t i = 0; i + 1 < sv.size(); ++i) {
        if (sv[i] == '\r' && sv[i + 1] == '\n') {
            return i;
        }
    }
    return std::string_view::npos;
}

// Parse a signed 64-bit integer from a string slice (without CRLF).
// Returns true on success, false if invalid format or overflow.
bool parse_int64(std::string_view sv, int64_t& out) {
    if (sv.empty()) {
        return false;
    }

    // Handle optional leading '+'
    if (sv[0] == '+') {
        sv.remove_prefix(1);
        if (sv.empty()) return false;
    }

    const char* first = sv.data();
    const char* last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);

    // Ensure all characters were consumed and no error occurred
    return (ec == std::errc{} && ptr == last);
}

} // anonymous namespace

// ─── RespValue Implementation ───────────────────────────────────────────────

RespValue::RespValue()
    : type_(RespType::Null)
    , int_val_(0)
    , is_null_(true) {}

RespValue RespValue::make_simple_string(const std::string& s) {
    RespValue v;
    v.type_ = RespType::SimpleString;
    v.str_val_ = s;
    v.is_null_ = false;
    return v;
}

RespValue RespValue::make_error(const std::string& err) {
    RespValue v;
    v.type_ = RespType::Error;
    v.str_val_ = err;
    v.is_null_ = false;
    return v;
}

RespValue RespValue::make_integer(int64_t val) {
    RespValue v;
    v.type_ = RespType::Integer;
    v.int_val_ = val;
    v.is_null_ = false;
    return v;
}

RespValue RespValue::make_bulk_string(const std::string& s) {
    RespValue v;
    v.type_ = RespType::BulkString;
    v.str_val_ = s;
    v.is_null_ = false;
    return v;
}

RespValue RespValue::make_null_bulk_string() {
    RespValue v;
    v.type_ = RespType::BulkString;
    v.is_null_ = true;
    return v;
}

RespValue RespValue::make_array(const std::vector<RespValue>& elements) {
    RespValue v;
    v.type_ = RespType::Array;
    v.array_val_ = elements;
    v.is_null_ = false;
    return v;
}

RespValue RespValue::make_null_array() {
    RespValue v;
    v.type_ = RespType::Array;
    v.is_null_ = true;
    return v;
}

RespType RespValue::type() const { return type_; }
bool RespValue::is_simple_string() const { return type_ == RespType::SimpleString; }
bool RespValue::is_error() const { return type_ == RespType::Error; }
bool RespValue::is_integer() const { return type_ == RespType::Integer; }
bool RespValue::is_bulk_string() const { return type_ == RespType::BulkString && !is_null_; }
bool RespValue::is_array() const { return type_ == RespType::Array && !is_null_; }
bool RespValue::is_null() const { return is_null_; }

const std::string& RespValue::string_value() const { return str_val_; }
int64_t RespValue::integer_value() const { return int_val_; }
const std::vector<RespValue>& RespValue::array_value() const { return array_val_; }

bool RespValue::to_string_vector(std::vector<std::string>& out) const {
    if (!is_array()) {
        return false;
    }
    out.clear();
    out.reserve(array_val_.size());
    for (const auto& elem : array_val_) {
        if (elem.is_bulk_string() || elem.is_simple_string()) {
            out.push_back(elem.string_value());
        } else {
            return false;
        }
    }
    return true;
}

std::string RespValue::serialize() const {
    if (is_null_) {
        if (type_ == RespType::Array) {
            return RespEncoder::null_array();
        }
        return RespEncoder::null_bulk_string();
    }

    switch (type_) {
        case RespType::SimpleString:
            return RespEncoder::simple_string(str_val_);
        case RespType::Error:
            return RespEncoder::error(str_val_);
        case RespType::Integer:
            return RespEncoder::integer(int_val_);
        case RespType::BulkString:
            return RespEncoder::bulk_string(str_val_);
        case RespType::Array:
            return RespEncoder::array(array_val_);
        case RespType::Null:
            return RespEncoder::null_bulk_string();
    }
    return RespEncoder::null_bulk_string();
}

bool RespValue::operator==(const RespValue& other) const {
    if (type_ != other.type_ || is_null_ != other.is_null_) {
        return false;
    }
    if (is_null_) {
        return true;
    }
    switch (type_) {
        case RespType::SimpleString:
        case RespType::Error:
        case RespType::BulkString:
            return str_val_ == other.str_val_;
        case RespType::Integer:
            return int_val_ == other.int_val_;
        case RespType::Array:
            return array_val_ == other.array_val_;
        case RespType::Null:
            return true;
    }
    return false;
}

bool RespValue::operator!=(const RespValue& other) const {
    return !(*this == other);
}

// ─── RespEncoder Implementation ─────────────────────────────────────────────

std::string RespEncoder::simple_string(const std::string& s) {
    return "+" + s + "\r\n";
}

std::string RespEncoder::error(const std::string& err_msg) {
    return "-" + err_msg + "\r\n";
}

std::string RespEncoder::integer(int64_t val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string RespEncoder::bulk_string(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string RespEncoder::bulk_string(const char* data, std::size_t len) {
    return "$" + std::to_string(len) + "\r\n" + std::string(data, len) + "\r\n";
}

std::string RespEncoder::null_bulk_string() {
    return "$-1\r\n";
}

std::string RespEncoder::array(const std::vector<RespValue>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += item.serialize();
    }
    return result;
}

std::string RespEncoder::null_array() {
    return "*-1\r\n";
}

// ─── RespParser Implementation ─────────────────────────────────────────────

void RespParser::append(const char* data, std::size_t length) {
    buffer_.append(data, length);
}

void RespParser::append(const std::string& data) {
    buffer_.append(data);
}

void RespParser::reset() {
    buffer_.clear();
}

std::size_t RespParser::buffer_size() const {
    return buffer_.size();
}

ParseStatus RespParser::parse_next(RespValue& out) {
    if (buffer_.empty()) {
        return ParseStatus::Incomplete;
    }

    std::size_t bytes_consumed = 0;
    ParseStatus status = parse(buffer_, out, bytes_consumed);

    if (status == ParseStatus::Ok) {
        buffer_.erase(0, bytes_consumed);
    }
    return status;
}

ParseStatus RespParser::parse(std::string_view input, RespValue& out, std::size_t& bytes_consumed) {
    bytes_consumed = 0;

    if (input.empty()) {
        return ParseStatus::Incomplete;
    }

    char prefix = input[0];

    switch (prefix) {
        // ── 1. Simple String (+<string>\r\n) ──────────────────────────────
        case '+': {
            std::size_t crlf_pos = find_crlf(input);
            if (crlf_pos == std::string_view::npos) {
                return ParseStatus::Incomplete;
            }
            // Simple strings cannot contain CR or LF within them
            std::string_view content = input.substr(1, crlf_pos - 1);
            out = RespValue::make_simple_string(std::string(content));
            bytes_consumed = crlf_pos + 2;
            return ParseStatus::Ok;
        }

        // ── 2. Error (-<message>\r\n) ─────────────────────────────────────
        case '-': {
            std::size_t crlf_pos = find_crlf(input);
            if (crlf_pos == std::string_view::npos) {
                return ParseStatus::Incomplete;
            }
            std::string_view content = input.substr(1, crlf_pos - 1);
            out = RespValue::make_error(std::string(content));
            bytes_consumed = crlf_pos + 2;
            return ParseStatus::Ok;
        }

        // ── 3. Integer (:[<+|->]<digits>\r\n) ─────────────────────────────
        case ':': {
            std::size_t crlf_pos = find_crlf(input);
            if (crlf_pos == std::string_view::npos) {
                return ParseStatus::Incomplete;
            }
            std::string_view num_str = input.substr(1, crlf_pos - 1);
            int64_t val = 0;
            if (!parse_int64(num_str, val)) {
                return ParseStatus::Error;
            }
            out = RespValue::make_integer(val);
            bytes_consumed = crlf_pos + 2;
            return ParseStatus::Ok;
        }

        // ── 4. Bulk String ($<len>\r\n<data>\r\n) ─────────────────────────
        case '$': {
            std::size_t first_crlf = find_crlf(input);
            if (first_crlf == std::string_view::npos) {
                return ParseStatus::Incomplete;
            }

            std::string_view len_str = input.substr(1, first_crlf - 1);
            int64_t len = 0;
            if (!parse_int64(len_str, len)) {
                return ParseStatus::Error;
            }

            // Null Bulk String ($-1\r\n)
            if (len == -1) {
                out = RespValue::make_null_bulk_string();
                bytes_consumed = first_crlf + 2;
                return ParseStatus::Ok;
            }

            // Negative lengths < -1 are invalid
            if (len < -1) {
                return ParseStatus::Error;
            }

            // Total bytes required: header + CRLF + len bytes + trailing CRLF (2)
            std::size_t data_start = first_crlf + 2;
            std::size_t total_needed = data_start + static_cast<std::size_t>(len) + 2;

            if (input.size() < total_needed) {
                return ParseStatus::Incomplete;
            }

            // Check trailing CRLF
            if (input[data_start + len] != '\r' || input[data_start + len + 1] != '\n') {
                return ParseStatus::Error;
            }

            std::string_view content = input.substr(data_start, len);
            out = RespValue::make_bulk_string(std::string(content));
            bytes_consumed = total_needed;
            return ParseStatus::Ok;
        }

        // ── 5. Array (*<count>\r\n<elements...>) ──────────────────────────
        case '*': {
            std::size_t first_crlf = find_crlf(input);
            if (first_crlf == std::string_view::npos) {
                return ParseStatus::Incomplete;
            }

            std::string_view count_str = input.substr(1, first_crlf - 1);
            int64_t count = 0;
            if (!parse_int64(count_str, count)) {
                return ParseStatus::Error;
            }

            // Null Array (*-1\r\n)
            if (count == -1) {
                out = RespValue::make_null_array();
                bytes_consumed = first_crlf + 2;
                return ParseStatus::Ok;
            }

            // Negative counts < -1 are invalid
            if (count < -1) {
                return ParseStatus::Error;
            }

            // Empty Array (*0\r\n)
            if (count == 0) {
                out = RespValue::make_array({});
                bytes_consumed = first_crlf + 2;
                return ParseStatus::Ok;
            }

            // Recursively parse `count` elements
            std::size_t current_offset = first_crlf + 2;
            std::vector<RespValue> elements;
            elements.reserve(static_cast<std::size_t>(count));

            for (int64_t i = 0; i < count; ++i) {
                if (current_offset >= input.size()) {
                    return ParseStatus::Incomplete;
                }

                std::string_view remaining = input.substr(current_offset);
                RespValue elem;
                std::size_t elem_consumed = 0;

                ParseStatus elem_status = parse(remaining, elem, elem_consumed);
                if (elem_status != ParseStatus::Ok) {
                    return elem_status;
                }

                elements.push_back(std::move(elem));
                current_offset += elem_consumed;
            }

            out = RespValue::make_array(elements);
            bytes_consumed = current_offset;
            return ParseStatus::Ok;
        }

        default:
            // Unknown RESP prefix byte -> protocol error
            return ParseStatus::Error;
    }
}
