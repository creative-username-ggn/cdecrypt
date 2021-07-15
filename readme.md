# CDecrypt
## Build:
### Linux/Macos: 
**This requires `libssl-dev` on Ubuntu or equivalent for your linux distribution.**
`g++ main.cpp -lssl -lcrypto -o cdecrypt`
### Windows:
Ensure you have OpenSSL binaries installed. If you do not, you can obtain them [here](https://slproweb.com/products/Win32OpenSSL.html).
Make sure that you have OpenSSL set in your environment variables, as such:
```
set LIB=C:\OpenSSL-win64\lib;%LIB%
set INCLUDE=C:\OpenSSL-win64\include;%INCLUDE%
```
Navigate into the folder and run `cl main-win.cpp` to compile an .exe.

## Usage:
Place in a folder with encrypted Wii U Content.
Run `cdecrypt title.tmd title.tik`, the contents will be decrypted and placed into /code, /content, /meta folders.