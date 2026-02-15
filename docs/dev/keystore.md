# How ADB manages keys

## ADBD

 The details of the authentication protocol are found in [protocol.md](protocol.md) but, in short, it
relies on:

1. `adbd` issues an `AUTH` challenge to the workstation.
2. The workstation answers the `AUTH` challenge using its private RSA key.
3. `adbd` searches for the key in its list of "trusted devices" public RSA keys.
4. If found, the workstation is authenticated and the device goes into [kCsHost](https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:packages/modules/adb/adb.h;l=119;drc=f748699df1efef36a3fe82c9d2c72f19d547cc7b) state.
Otherwise, it goes in [kCsUnauthorized](https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:packages/modules/adb/adb.h;l=110;drc=f748699df1efef36a3fe82c9d2c72f19d547cc7b) state.

`adbd`'s source of truth for public RSA keys is contained in two files.

```
/adb_keys
/data/misc/adb/adb_keys
```

These are the only files used by `adbd` to verify a challenge answer. They are read [directly](https://cs.android.com/android/platform/superproject/main/+/main:frameworks/native/libs/adbd_auth/adbd_auth.cpp;l=412;drc=a9546201f8aadc436e5c8d4d75e9b8467122fe44)
by the library `adbd_auth`.


`/adb_keys` (a.k.a "adb_keys", a.k.a "system keys") is immutable and stored on read-only partitions. It ships with the device and contains
the vendor's public key. It is only present on `eng` and `user-debug` builds. On `user` builds, this file should not exist (as per [CTS test](https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:cts/tests/tests/os/src/android/os/cts/UsbDebuggingTest.java;l=40;drc=b748a8705746f423bcac28fd0c9a7994c67fbddd)).

`/data/misc/adb/adb_keys` (a.k.a "adb_user_keys") is read by `adbd` but can be modified by Framework. It is stored on a `rw` partition.

Both of these files use a simple "ADBD file format" made of one public key per line.

```
$ cat /adb_keys
[base64-key-X] android-eng@google.com
[base64-key-Y] sanglardf@google.com
```
## Framework

Framework is the component in charge of maintaining `adb_user_keys`.

When an `AUTH` challenge reply reaches `adbd`, it first iterates over all RSA public keys available in `adb_keys` and `adb_user_keys`
until it finds a match. If it does, the connection is authenticated and the workstation gains access to the device.

If no match is found, `adbd` uses the `adbd_auth` library to talk to Framework. This in turns results in a prompt asking the device user
if they want to trust this workstation. In this dialog, the whole public key is not displayed but rather its "fingerprint", which
is a MD5 hash of the RSA public key.

*Note on library `adbd_auth`*: This library was originally only intended to carry message about keys and authentication (as its name indicates).
However it has since been piggybacked on to carry ADB Wifi messages. It could be renamed `adbd_frameworks_comm` but that would be a lot of work
for a small payoff.

If the user clicks "yes", the connection is established. Moreover, users can also check the box "Always allow" which prompts
Framework to add the workstation public key to `adb_user_keys`.

### Key expiration

To guard against cases where a user would enable "Developer mode" once and forget about it, `adb_user_keys` entries expire after a week if
they are not used.

This expiration mechanism is implemented on the Frameworks side with file `/data/misc/adb/adb_temp_keys.xml`. This file does not contain ADB
format entries. Instead, it uses XML to allow for "Last connection" storage. It is commonly referred to as `adb_temp_keys`.

```
<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<adbKey key="base64-key-X user@server" lastConnection="1616486869211" />
<adbKey key="base64-key-Y user2@another_server" lastConnection="1616487035512" />
```

Each time a device connects, `adbd` sends this `MESSAGE_ADB_CONNECTED_KEY` event to Frameworks via `adbd_auth`. Frameworks then schedules a "key refresh".
When the refresh request triggers, Frameworks reads the `adb_temp_keys` entries, deletes expired keys and overwrites `adb_user_keys`.

### Key preservation on factory reset

The Frameworks part of ADB exposes the location of its "adb_keys" and "adb_tmp_user_keys" files. This is done so testing harnesses such
as Tradefed can back them up on a "Persistent Data Block" (see Factory Recovery Protection (FRP) for more information).
After the device is factory reset, the key files are restored automatically by the test harness.

Exposing the location of these files is far from ideal. For this reason, ADB team plans to change the API to improve encapsulation
(see b/428760829).
