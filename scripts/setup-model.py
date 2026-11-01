#!/usr/bin/env python3
"""Fetch the pinned YuNet model and notices, verifying SHA-256 before replacing files."""
import hashlib
from pathlib import Path
import urllib.request
ROOT = Path(__file__).resolve().parents[1]
COMMIT = '48b10fa505aeb51fbd127fc60a5c660040e823b7'
FILES = {
 'data/models/face_detection_yunet_2023mar.onnx': (
  f'https://media.githubusercontent.com/media/opencv/opencv_zoo/{COMMIT}/models/face_detection_yunet/face_detection_yunet_2023mar.onnx', '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4'),
 'data/licenses/YuNet-LICENSE': (
  f'https://raw.githubusercontent.com/opencv/opencv_zoo/{COMMIT}/models/face_detection_yunet/LICENSE', 'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958'),
}
def main():
 for relative, (url, expected) in FILES.items():
  path = ROOT / relative
  if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == expected:
   print(f'OK {relative}'); continue
  data = urllib.request.urlopen(url, timeout=90).read()
  actual = hashlib.sha256(data).hexdigest()
  if actual != expected:
   raise RuntimeError(f'{relative}: SHA-256 mismatch: {actual}')
  path.parent.mkdir(parents=True, exist_ok=True)
  temporary = path.with_suffix('.download')
  temporary.write_bytes(data)
  temporary.replace(path)
  print(f'Installed {relative} ({len(data)} bytes)')
if __name__ == '__main__': main()
