//  Copyright (c) 2026 Alice-Trade Inc.
//  SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include <ArduinoJson.h>
#include "webui/ApiHandlers.hxx"

using namespace daliMQTT;

static void test_http_chunk_stream_buffering() {
    HttpChunkStream stream{};
    stream.req = nullptr;

    TEST_ASSERT_EQUAL_UINT32(0, stream.index);

    const char* str = "Hello DALI Chunk Stream";
    size_t len = strlen(str);
    size_t written = stream.write(reinterpret_cast<const uint8_t*>(str), len);

    TEST_ASSERT_EQUAL_UINT32(len, written);
    TEST_ASSERT_EQUAL_UINT32(len, stream.index);
    TEST_ASSERT_EQUAL_INT8('H', stream.buffer[0]);
}

static void test_arduinojson_streaming_compatibility() {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["count"] = 64;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (int i = 0; i < 10; ++i) {
        arr.add(i);
    }

    HttpChunkStream stream{};
    size_t bytesSerialized = serializeJson(doc, stream);
    TEST_ASSERT_TRUE(bytesSerialized > 0);
    TEST_ASSERT_EQUAL_UINT32(bytesSerialized, stream.index);
}

void run_http_stream_tests() {
    RUN_TEST(test_http_chunk_stream_buffering);
    RUN_TEST(test_arduinojson_streaming_compatibility);
}