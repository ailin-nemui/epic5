This version of epic includes FiSH support.

FiSH is is vaguely like base64-encoded blowfish encryption.  The following
behaviors were observed:

Issue 1: RADIX64 conversion
---------------------------
FiSH uses a RADIX64 conversion, which converts 6 bits of binary data into
1 printable byte.  Since 6 bits contains 64 possible values, you can select
any 64 printable ascii code points you want, and do the conversion that way.

FiSH uses different code points than base64 does.

Whereas Base64 converts every 3 input bytes (24 bits) into 4 output bytes, 
	(meaning the output is 133% the size of the original)
  FiSH converts every 4 input bytes (32 bits) into 6 output bytes.
	(meaning the output is 150% the size of the original)
  The high 4 bits of the final RADIX64 byte are zero.

Whereas Base64 processes the input in 4 byte chunks,
  FiSH processes the input in 8 byte chunks

Whereas Base64 processes the input left to right,
  FiSH processes the input right to left

Whereas Base64 accounts for "missing" bytes in a final packet when encoding
  FiSH pads out the final packet with nul bytes.

Whereas Base64 accounts for "missing" bytes in a final packet when decoding
  FiSH ignores a final packet that is too short.

Base64:   Byte 0, Byte 1, Byte 2 ->  Output 0, Output 1, Output 2, Output 3
FiSH:     Byte 7, Byte 6, Byte 5, Byte 4 -> Output 0, Output 1, Output 2,
					    Output 3, Output 4, Output 5,
	  Byte 3, Byte 2, Byte 1, Byte 0 -> Output 6, Output 7, Output 8,
					    Output 9, Output 10, Output 11

