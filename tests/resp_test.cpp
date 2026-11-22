// resp_test.cpp — Unit tests for the RESP2 parser and encoder.

#include "resp.h"

#include <iostream>
#include <string>
#include <vector>

// ─── Minimal Test Framework ──────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static bool current_test_failed = false;

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "    FAIL: " << #cond                                \
                      << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                           \
            current_test_failed = true;                                       \
        }                                                                     \
    } while (false)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        if ((expected) != (actual)) {                                         \
            std::cerr << "    FAIL: expected (" << #expected << " = "         \
                      << (expected) << ") == (" << #actual << " = "           \
                      << (actual) << ")"                                      \
                      << "  (" << __FILE__ << ":" << __LINE__ << ")"          \
                      << std::endl;                                           \
            current_test_failed = true;                                       \
        }                                                                     \
    } while (false)

#define RUN_TEST(test_func)                                                   \
    do {                                                                      \
        current_test_failed = false;                                          \
        std::cout << "  " << #test_func << "... ";                            \
        test_func();                                                          \
        tests_run++;                                                          \
        if (current_test_failed) {                                            \
            tests_failed++;                                                   \
            std::cout << "FAILED" << std::endl;                               \
        } else {                                                              \
            tests_passed++;                                                   \
            std::cout << "PASSED" << std::endl;                               \
        }                                                                     \
    } while (false)

// ─── Simple String Tests ─────────────────────────────────────────────────────

void test_parse_simple_string() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("+OK\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(5u, consumed);
    ASSERT_TRUE(val.is_simple_string());
    ASSERT_EQ(std::string("OK"), val.string_value());
}

void test_parse_simple_string_incomplete() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("+OK", val, consumed);

    ASSERT_EQ(ParseStatus::Incomplete, status);
    ASSERT_EQ(0u, consumed);
}

// ─── Error Tests ─────────────────────────────────────────────────────────────

void test_parse_error() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("-ERR unknown command 'foobar'\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_TRUE(val.is_error());
    ASSERT_EQ(std::string("ERR unknown command 'foobar'"), val.string_value());
}

void test_parse_error_incomplete() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("-ERR", val, consumed);

    ASSERT_EQ(ParseStatus::Incomplete, status);
}

// ─── Integer Tests ───────────────────────────────────────────────────────────

void test_parse_integer() {
    RespValue val;
    std::size_t consumed = 0;

    ASSERT_EQ(ParseStatus::Ok, RespParser::parse(":0\r\n", val, consumed));
    ASSERT_TRUE(val.is_integer());
    ASSERT_EQ(int64_t(0), val.integer_value());

    ASSERT_EQ(ParseStatus::Ok, RespParser::parse(":1000\r\n", val, consumed));
    ASSERT_EQ(int64_t(1000), val.integer_value());

    ASSERT_EQ(ParseStatus::Ok, RespParser::parse(":-42\r\n", val, consumed));
    ASSERT_EQ(int64_t(-42), val.integer_value());

    ASSERT_EQ(ParseStatus::Ok, RespParser::parse(":+99\r\n", val, consumed));
    ASSERT_EQ(int64_t(99), val.integer_value());
}

void test_parse_integer_malformed() {
    RespValue val;
    std::size_t consumed = 0;

    ASSERT_EQ(ParseStatus::Error, RespParser::parse(":abc\r\n", val, consumed));
    ASSERT_EQ(ParseStatus::Error, RespParser::parse(":\r\n", val, consumed));
    ASSERT_EQ(ParseStatus::Error, RespParser::parse(":+-\r\n", val, consumed));
}

// ─── Bulk String Tests ───────────────────────────────────────────────────────

void test_parse_bulk_string() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("$6\r\nfoobar\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(12u, consumed);
    ASSERT_TRUE(val.is_bulk_string());
    ASSERT_EQ(std::string("foobar"), val.string_value());
}

void test_parse_bulk_string_empty() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("$0\r\n\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(6u, consumed);
    ASSERT_TRUE(val.is_bulk_string());
    ASSERT_EQ(std::string(""), val.string_value());
}

void test_parse_bulk_string_null() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("$-1\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(5u, consumed);
    ASSERT_TRUE(val.is_null());
}

void test_parse_bulk_string_with_crlf_inside() {
    // Binary-safe: payload contains \r\n inside it!
    std::string payload = "hello\r\nworld";
    std::string wire = "$" + std::to_string(payload.size()) + "\r\n" + payload + "\r\n";

    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse(wire, val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_TRUE(val.is_bulk_string());
    ASSERT_EQ(payload, val.string_value());
}

void test_parse_bulk_string_incomplete() {
    RespValue val;
    std::size_t consumed = 0;

    // Incomplete length header
    ASSERT_EQ(ParseStatus::Incomplete, RespParser::parse("$5\r", val, consumed));

    // Incomplete payload
    ASSERT_EQ(ParseStatus::Incomplete, RespParser::parse("$5\r\nhel", val, consumed));

    // Missing trailing \n
    ASSERT_EQ(ParseStatus::Incomplete, RespParser::parse("$5\r\nhello\r", val, consumed));
}

void test_parse_bulk_string_malformed() {
    RespValue val;
    std::size_t consumed = 0;

    // Invalid length
    ASSERT_EQ(ParseStatus::Error, RespParser::parse("$abc\r\n", val, consumed));

    // Negative length < -1
    ASSERT_EQ(ParseStatus::Error, RespParser::parse("$-2\r\n", val, consumed));

    // Bad trailing CRLF
    ASSERT_EQ(ParseStatus::Error, RespParser::parse("$5\r\nhelloXX", val, consumed));
}

// ─── Array Tests ─────────────────────────────────────────────────────────────

void test_parse_array_empty() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("*0\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(4u, consumed);
    ASSERT_TRUE(val.is_array());
    ASSERT_EQ(0u, val.array_value().size());
}

void test_parse_array_null() {
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse("*-1\r\n", val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(5u, consumed);
    ASSERT_TRUE(val.is_null());
}

void test_parse_array_commands() {
    // Typical Redis command: *2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n
    std::string wire = "*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n";
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse(wire, val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(wire.size(), consumed);
    ASSERT_TRUE(val.is_array());
    ASSERT_EQ(2u, val.array_value().size());
    ASSERT_EQ(std::string("ECHO"), val.array_value()[0].string_value());
    ASSERT_EQ(std::string("hello"), val.array_value()[1].string_value());

    // Test conversion to string vector
    std::vector<std::string> args;
    ASSERT_TRUE(val.to_string_vector(args));
    ASSERT_EQ(2u, args.size());
    ASSERT_EQ(std::string("ECHO"), args[0]);
    ASSERT_EQ(std::string("hello"), args[1]);
}

void test_parse_array_mixed() {
    // Array with integer, simple string, bulk string
    std::string wire = "*3\r\n:1\r\n+OK\r\n$3\r\nfoo\r\n";
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse(wire, val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(3u, val.array_value().size());
    ASSERT_EQ(int64_t(1), val.array_value()[0].integer_value());
    ASSERT_EQ(std::string("OK"), val.array_value()[1].string_value());
    ASSERT_EQ(std::string("foo"), val.array_value()[2].string_value());
}

void test_parse_array_nested() {
    // Nested array: *2\r\n*1\r\n:99\r\n$3\r\nbar\r\n
    std::string wire = "*2\r\n*1\r\n:99\r\n$3\r\nbar\r\n";
    RespValue val;
    std::size_t consumed = 0;
    auto status = RespParser::parse(wire, val, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(2u, val.array_value().size());

    const auto& inner = val.array_value()[0];
    ASSERT_TRUE(inner.is_array());
    ASSERT_EQ(1u, inner.array_value().size());
    ASSERT_EQ(int64_t(99), inner.array_value()[0].integer_value());
    ASSERT_EQ(std::string("bar"), val.array_value()[1].string_value());
}

void test_parse_array_incomplete() {
    RespValue val;
    std::size_t consumed = 0;

    // Missing elements
    ASSERT_EQ(ParseStatus::Incomplete, RespParser::parse("*2\r\n$3\r\nfoo\r\n", val, consumed));

    // Element partially arrived
    ASSERT_EQ(ParseStatus::Incomplete, RespParser::parse("*2\r\n$3\r\nfoo\r\n$3\r\nba", val, consumed));
}

void test_parse_array_malformed() {
    RespValue val;
    std::size_t consumed = 0;

    ASSERT_EQ(ParseStatus::Error, RespParser::parse("*abc\r\n", val, consumed));
    ASSERT_EQ(ParseStatus::Error, RespParser::parse("*-5\r\n", val, consumed));
}

// ─── Streaming & Fragmented TCP Tests ────────────────────────────────────────

void test_streaming_fragmented_byte_by_byte() {
    // Simulate streaming TCP input arriving 1 byte at a time!
    std::string wire = "*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n";
    RespParser parser;
    RespValue val;

    for (std::size_t i = 0; i < wire.size() - 1; ++i) {
        parser.append(&wire[i], 1);
        ASSERT_EQ(ParseStatus::Incomplete, parser.parse_next(val));
    }

    // Append the very last byte '\n'
    parser.append(&wire[wire.size() - 1], 1);
    ASSERT_EQ(ParseStatus::Ok, parser.parse_next(val));

    std::vector<std::string> args;
    ASSERT_TRUE(val.to_string_vector(args));
    ASSERT_EQ(3u, args.size());
    ASSERT_EQ(std::string("SET"), args[0]);
    ASSERT_EQ(std::string("mykey"), args[1]);
    ASSERT_EQ(std::string("myvalue"), args[2]);

    // Buffer should now be empty
    ASSERT_EQ(0u, parser.buffer_size());
}

void test_streaming_multiple_messages_pipelined() {
    // Simulate multiple pipelined commands received in a single chunk
    std::string chunk = "+OK\r\n+PONG\r\n:123\r\n";
    RespParser parser;
    parser.append(chunk);

    RespValue v1, v2, v3;
    ASSERT_EQ(ParseStatus::Ok, parser.parse_next(v1));
    ASSERT_TRUE(v1.is_simple_string());
    ASSERT_EQ(std::string("OK"), v1.string_value());

    ASSERT_EQ(ParseStatus::Ok, parser.parse_next(v2));
    ASSERT_TRUE(v2.is_simple_string());
    ASSERT_EQ(std::string("PONG"), v2.string_value());

    ASSERT_EQ(ParseStatus::Ok, parser.parse_next(v3));
    ASSERT_TRUE(v3.is_integer());
    ASSERT_EQ(int64_t(123), v3.integer_value());

    // Buffer empty
    ASSERT_EQ(0u, parser.buffer_size());
    ASSERT_EQ(ParseStatus::Incomplete, parser.parse_next(v1));
}

void test_unknown_prefix_returns_error() {
    RespValue val;
    std::size_t consumed = 0;
    ASSERT_EQ(ParseStatus::Error, RespParser::parse("?what_is_this\r\n", val, consumed));
}

// ─── Encoder Tests ───────────────────────────────────────────────────────────

void test_encode_simple_string() {
    std::string encoded = RespEncoder::simple_string("OK");
    ASSERT_EQ(std::string("+OK\r\n"), encoded);
}

void test_encode_error() {
    std::string encoded = RespEncoder::error("ERR unknown command");
    ASSERT_EQ(std::string("-ERR unknown command\r\n"), encoded);
}

void test_encode_integer() {
    ASSERT_EQ(std::string(":0\r\n"), RespEncoder::integer(0));
    ASSERT_EQ(std::string(":42\r\n"), RespEncoder::integer(42));
    ASSERT_EQ(std::string(":-100\r\n"), RespEncoder::integer(-100));
}

void test_encode_bulk_string() {
    ASSERT_EQ(std::string("$5\r\nhello\r\n"), RespEncoder::bulk_string("hello"));
    ASSERT_EQ(std::string("$0\r\n\r\n"), RespEncoder::bulk_string(""));
    ASSERT_EQ(std::string("$-1\r\n"), RespEncoder::null_bulk_string());
}

void test_encode_array() {
    std::vector<RespValue> items = {
        RespValue::make_bulk_string("LLEN"),
        RespValue::make_bulk_string("mylist")
    };
    std::string encoded = RespEncoder::array(items);
    ASSERT_EQ(std::string("*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n"), encoded);
    ASSERT_EQ(std::string("*-1\r\n"), RespEncoder::null_array());
}

void test_roundtrip_encode_and_parse() {
    // Create complex value, serialize it, parse it back, verify equality
    std::vector<RespValue> original_elements = {
        RespValue::make_simple_string("PONG"),
        RespValue::make_integer(42),
        RespValue::make_bulk_string("supercalifragilistic"),
        RespValue::make_null_bulk_string()
    };
    RespValue original = RespValue::make_array(original_elements);

    std::string wire = original.serialize();

    RespValue parsed;
    std::size_t consumed = 0;
    auto status = RespParser::parse(wire, parsed, consumed);

    ASSERT_EQ(ParseStatus::Ok, status);
    ASSERT_EQ(wire.size(), consumed);
    ASSERT_TRUE(original == parsed);
}

// ─── Test Runner ─────────────────────────────────────────────────────────────

int main() {
    std::cout << std::endl;
    std::cout << "=== mini-redis RESP Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Simple Strings]" << std::endl;
    RUN_TEST(test_parse_simple_string);
    RUN_TEST(test_parse_simple_string_incomplete);
    std::cout << std::endl;

    std::cout << "[Errors]" << std::endl;
    RUN_TEST(test_parse_error);
    RUN_TEST(test_parse_error_incomplete);
    std::cout << std::endl;

    std::cout << "[Integers]" << std::endl;
    RUN_TEST(test_parse_integer);
    RUN_TEST(test_parse_integer_malformed);
    std::cout << std::endl;

    std::cout << "[Bulk Strings]" << std::endl;
    RUN_TEST(test_parse_bulk_string);
    RUN_TEST(test_parse_bulk_string_empty);
    RUN_TEST(test_parse_bulk_string_null);
    RUN_TEST(test_parse_bulk_string_with_crlf_inside);
    RUN_TEST(test_parse_bulk_string_incomplete);
    RUN_TEST(test_parse_bulk_string_malformed);
    std::cout << std::endl;

    std::cout << "[Arrays]" << std::endl;
    RUN_TEST(test_parse_array_empty);
    RUN_TEST(test_parse_array_null);
    RUN_TEST(test_parse_array_commands);
    RUN_TEST(test_parse_array_mixed);
    RUN_TEST(test_parse_array_nested);
    RUN_TEST(test_parse_array_incomplete);
    RUN_TEST(test_parse_array_malformed);
    std::cout << std::endl;

    std::cout << "[Streaming & Fragmentation]" << std::endl;
    RUN_TEST(test_streaming_fragmented_byte_by_byte);
    RUN_TEST(test_streaming_multiple_messages_pipelined);
    RUN_TEST(test_unknown_prefix_returns_error);
    std::cout << std::endl;

    std::cout << "[Encoder & Roundtrip]" << std::endl;
    RUN_TEST(test_encode_simple_string);
    RUN_TEST(test_encode_error);
    RUN_TEST(test_encode_integer);
    RUN_TEST(test_encode_bulk_string);
    RUN_TEST(test_encode_array);
    RUN_TEST(test_roundtrip_encode_and_parse);
    std::cout << std::endl;

    // --- Summary ---
    std::cout << "================================" << std::endl;
    std::cout << "Total: " << tests_run
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;

    if (tests_failed == 0) {
        std::cout << "All tests PASSED!" << std::endl;
    } else {
        std::cerr << "Some tests FAILED." << std::endl;
    }

    std::cout << "================================" << std::endl;
    std::cout << std::endl;

    return (tests_failed == 0) ? 0 : 1;
}
