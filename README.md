# uefi_message_check_error
EFI program printing Intel Message Check errors.

The program starts by looking for previous Message Check errors by reading content of Message-Specific Registers (MSRs). Then new Message Check errors are displayed.

To install all requirements to build this project :
```
sudo apt-get install gnu-efi
```

To build this project type :
```
$ make
```

To build a disk image and test it with qemu :
```
$ ./build_and_test.sh
> fs0:
> main.efi
```

To build a disk image ready to burn it to a USB mass storage key :
```
$ ./build.sh
$ sudo dd if=uefi.img dd=/dev/sdc
```
Assuming */dev/sdc* is your USB mass storage key.

