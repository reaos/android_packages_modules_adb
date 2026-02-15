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

#include "adb_utils.h"
#include "adb_wifi.h"
#include "sysdeps.h"

class AdbWifiKnownHostsTest : public ::testing::Test {
  public:
    void SetUp() { known_hosts_.Clear(); }

    void TearDown() { known_hosts_.Clear(); }

  protected:
    KnownWifiHostsFile known_hosts_ =
            KnownWifiHostsFile(adb_get_android_dir_path() + "/adb_wifi_keystore_test.pb");
};

TEST_F(AdbWifiKnownHostsTest, addKnownHost) {
    std::string host = "adb-14141FDF600081-TnSdi9";
    ASSERT_FALSE(known_hosts_.IsKnownHost(host));

    known_hosts_.AddKnownHost(host);
    ASSERT_TRUE(known_hosts_.IsKnownHost(host));
}

TEST_F(AdbWifiKnownHostsTest, allowDuplicates) {
    std::string host = "adb-14141FDF600081-TnSdi9";
    ASSERT_FALSE(known_hosts_.IsKnownHost(host));

    known_hosts_.AddKnownHost(host);
    ASSERT_TRUE(known_hosts_.IsKnownHost(host));

    // Make sure a duplicate is still detected as a known host.
    ASSERT_TRUE(known_hosts_.IsKnownHost(host + " (1)"));
}