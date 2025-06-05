/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define TRACE_TAG TRANSPORT

#include "sysdeps.h"
#include "transport.h"

#include <errno.h>
#include <linux/vm_sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <android-base/parsenetaddress.h>
#include <android-base/stringprintf.h>
#include <android-base/thread_annotations.h>
#include <cutils/sockets.h>

#include <android-base/properties.h>

#if defined(__ANDROID__) && !defined(__ANDROID_RECOVERY__)
#include <com_android_adbd_flags.h>
#endif

#include "adb.h"
#include "adb_io.h"
#include "adb_unique_fd.h"
#include "adb_utils.h"
#include "socket_spec.h"
#include "sysdeps/chrono.h"

static bool should_check_vsock_cid() {
#if defined(__ANDROID__) && !defined(__ANDROID_RECOVERY__)
    return com_android_adbd_flags_adbd_restrict_vsock_local_cid();
#endif
    return true;
}

static bool is_local_vsock_connection(const sockaddr_vm& server_addr,
                                      const sockaddr_vm& client_addr) {
    // In vsock address, CID is an identifier for detecting whether it's either a virtual machine or
    // the host of virtual machines. When the connection is from the local process, the address of
    // the server or the client contains VMADDR_CID_LOCAL or the machine's CID respectively. The
    // equality checks here is for restricting all possible 4 cases.
    return server_addr.svm_cid == VMADDR_CID_LOCAL || client_addr.svm_cid == VMADDR_CID_LOCAL ||
           server_addr.svm_cid == client_addr.svm_cid;
}

static unique_fd adb_vsock_accept(borrowed_fd serverfd) {
    sockaddr_vm server_addr, client_addr;
    socklen_t server_addr_len = sizeof(server_addr);
    socklen_t client_addr_len = sizeof(client_addr);

    unique_fd fd(adb_socket_accept(serverfd, reinterpret_cast<struct sockaddr*>(&client_addr),
                                   &client_addr_len));
    if (fd < 0) {
        VLOG(TRANSPORT) << "server: failed to adb_socket_accept";
        return {};
    }

    if (getsockname(fd.get(), reinterpret_cast<struct sockaddr*>(&server_addr), &server_addr_len) <
        0) {
        VLOG(TRANSPORT) << "server: failed to retrieve socket address of accept fd";
        return {};
    }

    if (server_addr.svm_family != AF_VSOCK || client_addr.svm_family != AF_VSOCK) {
        VLOG(TRANSPORT) << "server: invalid vsock address";
        return {};
    }

    // Adbd rejects local connection over vsock, to prevent connection establishment by any
    // arbitrary apps or processes unrelated to virtual machine.
    if (is_local_vsock_connection(server_addr, client_addr)) {
        VLOG(TRANSPORT) << "server: adbd restricts vsock connection from local";
        return {};
    }

    return fd;
}

void server_socket_thread(std::string_view addr) {
    adb_thread_setname("server_socket");

    unique_fd serverfd;
    std::string error;

    while (serverfd == -1) {
        errno = 0;
        serverfd = unique_fd{socket_spec_listen(addr, &error, nullptr)};
        if (serverfd < 0) {
            if (errno == EAFNOSUPPORT || errno == EINVAL || errno == EPROTONOSUPPORT) {
                D("unrecoverable error: '%s'", error.c_str());
                return;
            }
            D("server: cannot bind socket yet: %s", error.c_str());
            std::this_thread::sleep_for(1s);
            continue;
        }
        close_on_exec(serverfd.get());
    }

    while (true) {
        D("server: trying to get new connection from fd %d", serverfd.get());
        unique_fd fd;
        if (addr.starts_with("vsock:") && should_check_vsock_cid()) {
            fd = adb_vsock_accept(serverfd);
        } else {
            fd = unique_fd{adb_socket_accept(serverfd, nullptr, nullptr)};
        }
        if (fd >= 0) {
            D("server: new connection on fd %d", fd.get());
            close_on_exec(fd.get());
            disable_tcp_nagle(fd.get());
            std::string serial = android::base::StringPrintf("host-%d", fd.get());
            // We don't care about port value in "register_socket_transport" as it is used
            // only from ADB_HOST. "server_socket_thread" is never called from ADB_HOST.
            register_socket_transport(
                    std::move(fd), std::move(serial), 0, false,
                    [](atransport*) { return ReconnectResult::Abort; }, false);
        }
    }
    D("transport: server_socket_thread() exiting");
}

void init_transport_socket_server(const std::string& addr) {
    VLOG(TRANSPORT) << "Starting tcp server on '" << addr << "'";
    std::thread(server_socket_thread, addr).detach();
}

int init_socket_transport(atransport* t, unique_fd fd, int, bool) {
    t->type = kTransportLocal;
    auto fd_connection = std::make_unique<FdConnection>(std::move(fd));
    t->SetConnection(std::make_unique<BlockingConnectionAdapter>(std::move(fd_connection)));
    return 0;
}