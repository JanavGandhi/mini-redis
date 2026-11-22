// resp.h — Redis Serialization Protocol (RESP2) parser and encoder.
//
// RESP is the binary-safe wire protocol used by Redis. It is human-readable,
// easy to parse, and supports 5 primary data types:
//
//   Type             First Byte   Example Wire Format
//   --------------------------------------------------------------
//   Simple String    '+'          +OK\r\n
//   Error            '-'          -ERR unknown command 'foobar'\r\n
//   Integer          ':'          :1000\r\n
//   Bulk String      '$'          $6\r\nfoobar\r\n
//                                 $-1\r\n (Null bulk string)
//   Array            '*'          *2\r\n$3\r\nGET\r\n$5\r\nmykey\r\n
//                                 *-1\r\n (Null array)
//
// In RESP, all lines are terminated by CRLF ("\r\n").
// Redis commands sent by clients (like redis-cli) are ALWAYS sent as
// RESP Arrays of Bulk Strings.

#ifndef MINI_REDIS_RESP_H
#define MINI_REDIS_RESP_H

#include <cstdint>       // int64_t
#include <cstddef>       // std::size_t
#include <ostream>       // std::ostream (for operator<< printing)
#include <string>        // std::string
#include <string_view>   // std::string_view (C++17 zero-copy string slice)
#include <vector>        // std::vector

// ─── RESP Types ─────────────────────────────────────────────────────────────

enum class RespType {
    SimpleString,    // +<string>\r\n
    Error,           // -<error message>\r\n
    Integer,         // :[<+|->]<integer>\r\n
    BulkString,      // $<length>\r\n<data>\r\n
    Array,           // *<count>\r\n<element 1>...<element N>
    Null             // $-1\r\n (Null Bulk String) or *-1\r\n (Null Array)
};

inline std::ostream& operator<<(std::ostream& os, RespType type) {
    switch (type) {
        case RespType::SimpleString: return os << "RespType::SimpleString";
        case RespType::Error:        return os << "RespType::Error";
        case RespType::Integer:      return os << "RespType::Integer";
        case RespType::BulkString:   return os << "RespType::BulkString";
        case RespType::Array:        return os << "RespType::Array";
        case RespType::Null:         return os << "RespType::Null";
    }
    return os << "Unknown";
}

// ─── Parse Status ───────────────────────────────────────────────────────────

enum class ParseStatus {
    Ok,          // A complete RESP value was successfully parsed.
    Incomplete,  // The input buffer ended before a complete value arrived.
                 // More TCP chunks are needed.
    Error        // The input violated the RESP protocol (syntax error).
};

inline std::ostream& operator<<(std::ostream& os, ParseStatus status) {
    switch (status) {
        case ParseStatus::Ok:         return os << "ParseStatus::Ok";
        case ParseStatus::Incomplete: return os << "ParseStatus::Incomplete";
        case ParseStatus::Error:      return os << "ParseStatus::Error";
    }
    return os << "Unknown";
}

// ─── RespValue Class ────────────────────────────────────────────────────────

class RespValue {
public:
    // Default constructor: creates a Null value.
    RespValue();

    // Factory methods to create specific RESP types cleanly:
    static RespValue make_simple_string(const std::string& s);
    static RespValue make_error(const std::string& err);
    static RespValue make_integer(int64_t val);
    static RespValue make_bulk_string(const std::string& s);
    static RespValue make_null_bulk_string();
    static RespValue make_array(const std::vector<RespValue>& elements);
    static RespValue make_null_array();

    // Type queries
    RespType type() const;
    bool is_simple_string() const;
    bool is_error() const;
    bool is_integer() const;
    bool is_bulk_string() const;
    bool is_array() const;
    bool is_null() const;

    // Value getters (valid for their corresponding types)
    const std::string& string_value() const;
    int64_t integer_value() const;
    const std::vector<RespValue>& array_value() const;

    // Helper: converts a command array (e.g. *2\r\n$3\r\nGET\r\n$1\r\nx\r\n)
    // into a vector of strings (e.g. {"GET", "x"}).
    // Returns true if successfully converted, false if not an array or elements
    // cannot be represented as strings.
    bool to_string_vector(std::vector<std::string>& out) const;

    // Serialize this RespValue into its RESP wire format string.
    std::string serialize() const;

    // Equality operator for comparisons and testing.
    bool operator==(const RespValue& other) const;
    bool operator!=(const RespValue& other) const;

private:
    RespType type_;
    std::string str_val_;              // Used by SimpleString, Error, BulkString
    int64_t int_val_;                  // Used by Integer
    std::vector<RespValue> array_val_; // Used by Array
    bool is_null_;                     // True for $-1\r\n and *-1\r\n
};

// ─── RespEncoder Class ──────────────────────────────────────────────────────
// Helper class with static functions for direct wire encoding.

class RespEncoder {
public:
    static std::string simple_string(const std::string& s);
    static std::string error(const std::string& err_msg);
    static std::string integer(int64_t val);
    static std::string bulk_string(const std::string& s);
    static std::string bulk_string(const char* data, std::size_t len);
    static std::string null_bulk_string();
    static std::string array(const std::vector<RespValue>& items);
    static std::string null_array();
};

// ─── RespParser Class ───────────────────────────────────────────────────────
//
// Streaming parser capable of handling fragmented TCP input.
//
// TCP is a stream of bytes with no message boundaries. A message might arrive:
//   1. All in one recv() call (normal).
//   2. Split across multiple recv() calls (fragmented).
//   3. With multiple messages bundled into a single recv() call.
//
// RespParser buffers incoming bytes via append(), and caller repeatedly calls
// parse_next() to extract complete values.

class RespParser {
public:
    RespParser() = default;

    // Append newly received bytes from socket recv() into internal buffer.
    void append(const char* data, std::size_t length);
    void append(const std::string& data);

    // Try to parse the next complete RespValue from the buffer.
    // - If Ok: populates `out`, consumes parsed bytes from buffer.
    // - If Incomplete: leaves buffer unchanged, wait for more data via append().
    // - If Error: protocol error encountered (malformed input).
    ParseStatus parse_next(RespValue& out);

    // Direct static parse helper: parses one RespValue from a string_view.
    // On Ok: sets `bytes_consumed` to how many bytes were used.
    // On Incomplete: bytes_consumed is 0.
    // On Error: bytes_consumed is 0.
    static ParseStatus parse(std::string_view input, RespValue& out, std::size_t& bytes_consumed);

    // Clear the internal buffer (e.g. after an error or connection reset).
    void reset();

    // Check how many bytes are currently in the unparsed buffer.
    std::size_t buffer_size() const;

private:
    std::string buffer_;  // Internal streaming buffer
};

#endif // MINI_REDIS_RESP_H
