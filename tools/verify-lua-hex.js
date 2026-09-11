"use strict";

const crypto = require("crypto");
const fs = require("fs");

const FLASH_BASE = 0x08000000;
const FLASH_END = 0x08100000;
const ARCHIVE_ADDRESS = 0x080c0000;
const ARCHIVE_END = 0x080fe000;

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function readWord(memory, address) {
  return (memory.get(address) |
    (memory.get(address + 1) << 8) |
    (memory.get(address + 2) << 16) |
    (memory.get(address + 3) << 24)) >>> 0;
}

function parseHex(filename) {
  const memory = new Map();
  const lines = fs.readFileSync(filename, "ascii").trim().split(/\r?\n/);
  let addressBase = 0;
  let eofCount = 0;
  for (const [lineIndex, line] of lines.entries()) {
    const record = Buffer.from(line.slice(1), "hex");
    if (!/^:[0-9a-f]+$/i.test(line) || record.length !== record[0] + 5 ||
        (record.reduce((sum, byte) => sum + byte, 0) & 0xff) !== 0) {
      throw new Error(`Invalid Intel HEX record at line ${lineIndex + 1}.`);
    }
    const offset = record.readUInt16BE(1);
    const type = record[3];
    const data = record.subarray(4, 4 + record[0]);
    if (type === 0x00) {
      for (let index = 0; index < data.length; index++) {
        memory.set(addressBase + offset + index, data[index]);
      }
    } else if (type === 0x04) {
      addressBase = data.readUInt16BE(0) * 0x10000;
    } else if (type === 0x01) {
      eofCount++;
    }
  }
  if (eofCount !== 1) throw new Error(`Expected one EOF, found ${eofCount}.`);
  return memory;
}

const filename = process.argv[2];
if (!filename) throw new Error("Usage: node tools/verify-lua-hex.js <file.hex>");
const memory = parseHex(filename);
const stack = readWord(memory, FLASH_BASE);
const reset = readWord(memory, FLASH_BASE + 4);
const archiveLength = readWord(memory, ARCHIVE_ADDRESS + 8);
if (stack < 0x20000000 || stack > 0x20060000 ||
    reset < FLASH_BASE + 1 || reset >= ARCHIVE_ADDRESS || !(reset & 1)) {
  throw new Error("Invalid application vectors.");
}
if (archiveLength < 33 || archiveLength > ARCHIVE_END - ARCHIVE_ADDRESS) {
  throw new Error("Invalid archive length.");
}
const archive = Buffer.from(Array.from(
  { length: archiveLength }, (_, index) => memory.get(ARCHIVE_ADDRESS + index)
));
const sourceLength = archive.readUInt32LE(12);
const source = archive.subarray(32);
const archiveInput = Buffer.from(archive);
archiveInput.writeUInt32LE(0, 20);
if (archive.subarray(0, 4).toString("ascii") !== "SLUA" ||
    source.length !== sourceLength ||
    crc32(source) !== archive.readUInt32LE(16) ||
    crc32(archiveInput) !== archive.readUInt32LE(20)) {
  throw new Error("Invalid SLUA archive.");
}
for (const address of memory.keys()) {
  if (address < FLASH_BASE || address >= FLASH_END) {
    throw new Error(`Address 0x${address.toString(16)} exceeds MCU flash.`);
  }
  if (address >= ARCHIVE_END) {
    throw new Error(`Address 0x${address.toString(16)} overlaps settings flash.`);
  }
}
const hash = crypto.createHash("sha256").update(fs.readFileSync(filename))
  .digest("hex");
console.log(`Vector SP: 0x${stack.toString(16).toUpperCase().padStart(8, "0")}`);
console.log(`Reset vector: 0x${reset.toString(16).toUpperCase().padStart(8, "0")}`);
console.log(`SLUA archive: ${archiveLength} bytes at 0x080C0000`);
console.log(`SHA-256: ${hash}`);