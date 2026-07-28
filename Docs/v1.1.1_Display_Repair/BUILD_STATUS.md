# Build status

The firmware and plugin source were checked for archive integrity, JSON
validity, JavaScript syntax/loadability, manifest validity and balanced C++
braces. The final ZIP was extracted again and compared byte-for-byte with its
source firmware.

This workspace did not contain the ESP8266 3.1.2 platform or its Xtensa
toolchain, and its restricted network could not fetch them. Consequently this
package deliberately contains the corrected flash source and matched plugin,
but not a newly compiled `.bin`. Compile it using the target named in
`README.md` before flashing hardware.
