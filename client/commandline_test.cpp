/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "app_processes.pb.h"
#include "client/commandline.h"

std::size_t count_occurrences(const std::string& str, const std::string& substr) {
    size_t occurrences = 0;
    std::string::size_type pos = 0;
    while ((pos = str.find(substr, pos)) != std::string::npos) {
        occurrences += 1;
        pos += substr.length();
    }
    return occurrences;
}

std::string proto_to_hex4proto(const std::string& proto) {
    return android::base::StringPrintf("%04zx", proto.size()) + proto;
}

TEST(commandline, parse_full_proto) {
    adb::proto::AppProcesses processes;
    auto process = processes.add_process();
    std::string process_name = "foo4089";
    process->set_process_name(process_name);

    std::string proto;
    processes.SerializeToString(&proto);
    std::string hex4_proto = proto_to_hex4proto(proto);

    std::string out;
    std::string err;
    std::string message = "Testing123";
    auto converter = ProtoBinaryToText<adb::proto::AppProcesses>(message, &out, &err);
    converter.OnStdoutReceived(hex4_proto.data(), hex4_proto.size());

    ASSERT_FALSE(out.empty());
    ASSERT_TRUE(out.contains(message));
    ASSERT_EQ(1u, count_occurrences(out, message));
    ASSERT_EQ(1u, count_occurrences(out, process_name));
}

TEST(commandline, parse_full_proto_chopped_in_1_bytes) {
    adb::proto::AppProcesses processes;
    auto process = processes.add_process();
    std::string process_name = "foo4089";
    process->set_process_name(process_name);

    std::string proto;
    processes.SerializeToString(&proto);
    std::string hex4_proto = proto_to_hex4proto(proto);

    std::string out;
    std::string err;
    std::string message = "Testing123";
    auto converter = ProtoBinaryToText<adb::proto::AppProcesses>(message, &out, &err);
    for (auto i = 0u; i < hex4_proto.size(); i++) {
        converter.OnStdoutReceived(hex4_proto.data() + i, 1);
    }

    ASSERT_FALSE(out.empty());
    ASSERT_TRUE(out.contains(message));
    ASSERT_EQ(1u, count_occurrences(out, message));
    ASSERT_EQ(1u, count_occurrences(out, process_name));
}

TEST(commandline, parse_half_proto) {
    adb::proto::AppProcesses processes;
    auto process = processes.add_process();
    process->set_process_name("foo");

    std::string proto;
    processes.SerializeToString(&proto);
    std::string hex4_proto = proto_to_hex4proto(proto);

    std::string out;
    std::string err;
    std::string message = "Testing 123";
    auto converter = ProtoBinaryToText<adb::proto::AppProcesses>(message, &out, &err);
    converter.OnStdoutReceived(hex4_proto.data(), hex4_proto.size() / 2);
    ASSERT_TRUE(out.empty());
}

TEST(commandline, parse_two_proto) {
    adb::proto::AppProcesses processes1;
    auto process1 = processes1.add_process();
    std::string process_name1 = "foo4089";
    process1->set_process_name(process_name1);

    adb::proto::AppProcesses processes2;
    auto process2 = processes2.add_process();
    std::string process_name2 = "foo8098";
    process2->set_process_name(process_name2);

    std::string proto1;
    processes1.SerializeToString(&proto1);
    std::string hex4_proto1 = proto_to_hex4proto(proto1);

    std::string proto2;
    processes2.SerializeToString(&proto2);
    std::string hex4_proto2 = proto_to_hex4proto(proto2);

    std::string two_messages;
    two_messages.append(hex4_proto1);
    two_messages.append(hex4_proto2);
    std::string out;
    std::string err;
    std::string message = "Testing123";
    auto converter = ProtoBinaryToText<adb::proto::AppProcesses>(message, &out, &err);
    converter.OnStdoutReceived(two_messages.data(), two_messages.size());

    ASSERT_FALSE(out.empty());
    ASSERT_EQ(2u, count_occurrences(out, message));
    ASSERT_EQ(1u, count_occurrences(out, process_name1));
    ASSERT_EQ(1u, count_occurrences(out, process_name2));
}

TEST(commandline, parse_one_and_a_half_proto) {
    adb::proto::AppProcesses processes1;
    auto process1 = processes1.add_process();
    std::string process_name1 = "foo4089";
    process1->set_process_name(process_name1);

    adb::proto::AppProcesses processes2;
    auto process2 = processes2.add_process();
    std::string process_name2 = "foo8098";
    process2->set_process_name(process_name2);

    std::string proto1;
    processes1.SerializeToString(&proto1);
    std::string hex4_proto1 = proto_to_hex4proto(proto1);

    std::string proto2;
    processes2.SerializeToString(&proto2);
    std::string hex4_proto2 = proto_to_hex4proto(proto2);

    std::string two_messages;
    two_messages.append(hex4_proto1);
    two_messages.append(hex4_proto2.substr(0, hex4_proto2.size() / 2));
    std::string out;
    std::string err;
    std::string message = "Testing123";
    auto converter = ProtoBinaryToText<adb::proto::AppProcesses>(message, &out, &err);
    converter.OnStdoutReceived(two_messages.data(), two_messages.size());

    ASSERT_FALSE(out.empty());
    ASSERT_EQ(1u, count_occurrences(out, message));
    ASSERT_EQ(1u, count_occurrences(out, process_name1));
    ASSERT_EQ(0u, count_occurrences(out, process_name2));

    // Send the remainder of second proto
    out.clear();
    std::string remaining = hex4_proto2.substr(hex4_proto2.size() / 2, hex4_proto2.size());
    converter.OnStdoutReceived(remaining.data(), remaining.size());
    ASSERT_FALSE(out.empty());
    ASSERT_EQ(1u, count_occurrences(out, message));
    ASSERT_EQ(0u, count_occurrences(out, process_name1));
    ASSERT_EQ(1u, count_occurrences(out, process_name2));
}
