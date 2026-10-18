#!/usr/bin/env python3
"""Mã hóa libclient.so -> app/src/main/assets/libclient.so.enc  (AES-256-GCM, định dạng [12B IV][ciphertext+tag]).
Dùng:  pip install cryptography
       python tools/encrypt_client.py <libclient.so> <key_hex_64>
Cùng key_hex đó phải được nạp vào Keystore (dev: -PdevClientKeyHex=<key_hex>)."""
import os, sys
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

src, key_hex = sys.argv[1], sys.argv[2]
key = bytes.fromhex(key_hex)
assert len(key) == 32, "key phải 32 byte (64 ký tự hex)"
iv = os.urandom(12)
out = os.path.join(os.path.dirname(__file__), "..", "app", "src", "main", "assets", "libclient.so.enc")
with open(src, "rb") as f:
    blob = iv + AESGCM(key).encrypt(iv, f.read(), None)
with open(out, "wb") as f:
    f.write(blob)
print("OK ->", os.path.abspath(out), len(blob), "bytes")
