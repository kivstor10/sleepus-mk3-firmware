"use strict";

const fs = require("fs");
const path = require("path");

const FLASH_BASE = 0x08000000;
const LUA_ARCHIVE_ADDRESS = 0x080c0000;
const LUA_ARCHIVE_END = 0x080fe000;
const LUA_ARCHIVE_CAPACITY = LUA_ARCHIVE_END - LUA_ARCHIVE_ADDRESS;
const ARCHIVE_HEADER_SIZE = 32;
const ARCHIVE_VERSION = 1;
const HEX_RECORD_SIZE = 16;

function usage() {
  console.error(
    "Usage: node tools/package-lua.js --firmware <input.hex> " +
    "--script <script.lua> --output <combined.hex> " +
    "[--archive <archive.sleepus-pack>]"
  );
}

function parseArguments(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!name?.startsWith("--") || value === undefined) {
      throw new Error(`Invalid argument near ${name || "end of command"}.`);
    }
    options[name.slice(2)] = value;
  }
  for (const required of ["firmware", "script", "output"]) {
    if (!options[required]) throw new Error(`Missing --${required}.`);
  }
  return options;
}

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

function createArchive(script) {
  if (!script.length) throw new Error("Lua script is empty.");
  if (script.includes(0)) throw new Error("Lua source cannot contain NUL bytes.");
  if (script.length > LUA_ARCHIVE_CAPACITY - ARCHIVE_HEADER_SIZE) {
    throw new Error(
      `Lua source exceeds ${LUA_ARCHIVE_CAPACITY - ARCHIVE_HEADER_SIZE} bytes.`
    );
  }

  const archive = Buffer.alloc(ARCHIVE_HEADER_SIZE + script.length, 0xff);
  archive.write("SLUA", 0, 4, "ascii");
  archive.writeUInt16LE(ARCHIVE_VERSION, 4);
  archive.writeUInt16LE(ARCHIVE_HEADER_SIZE, 6);
  archive.writeUInt32LE(archive.length, 8);
  archive.writeUInt32LE(script.length, 12);
  archive.writeUInt32LE(crc32(script), 16);
  archive.writeUInt32LE(0, 20);
  archive.writeUInt32LE(0, 24);
  archive.writeUInt32LE(0, 28);
  script.copy(archive, ARCHIVE_HEADER_SIZE);
  archive.writeUInt32LE(crc32(archive), 20);
  return archive;
}

function parseHex(text) {
  const memory = new Map();
  const startRecords = [];
  let addressBase = 0;
  let eofSeen = false;
  const lines = text.split(/\r?\n/).filter(line => line.trim().length);

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const line = lines[lineIndex].trim();
    if (!/^:[0-9a-f]+$/i.test(line) || (line.length - 1) % 2 !== 0) {
      throw new Error(`Malformed Intel HEX at line ${lineIndex + 1}.`);
    }
    const record = Buffer.from(line.slice(1), "hex");
    const length = record[0];
    if (record.length !== length + 5) {
      throw new Error(`Wrong record length at line ${lineIndex + 1}.`);
    }
    const checksum = record.reduce((sum, byte) => sum + byte, 0) & 0xff;
    if (checksum !== 0) {
      throw new Error(`Bad Intel HEX checksum at line ${lineIndex + 1}.`);
    }

    const offset = record.readUInt16BE(1);
    const type = record[3];
    const data = record.subarray(4, 4 + length);
    if (type === 0x00) {
      const absolute = addressBase + offset;
      for (let index = 0; index < data.length; index++) {
        const address = absolute + index;
        if (memory.has(address) && memory.get(address) !== data[index]) {
          throw new Error(`Conflicting firmware data at 0x${address.toString(16)}.`);
        }
        memory.set(address, data[index]);
      }
    } else if (type === 0x01) {
      eofSeen = true;
    } else if (type === 0x02) {
      if (data.length !== 2) throw new Error("Invalid extended segment record.");
      addressBase = data.readUInt16BE(0) << 4;
    } else if (type === 0x04) {
      if (data.length !== 2) throw new Error("Invalid extended linear record.");
      addressBase = data.readUInt16BE(0) * 0x10000;
    } else if (type === 0x03 || type === 0x05) {
      startRecords.push({ type, data: Buffer.from(data) });
    } else {
      throw new Error(`Unsupported Intel HEX record type 0x${type.toString(16)}.`);
    }
  }
  if (!eofSeen) throw new Error("Intel HEX has no end-of-file record.");
  return { memory, startRecords };
}

function makeRecord(type, offset, data) {
  const record = Buffer.alloc(data.length + 5);
  record[0] = data.length;
  record.writeUInt16BE(offset, 1);
  record[3] = type;
  data.copy(record, 4);
  const sum = record.subarray(0, record.length - 1)
    .reduce((total, byte) => total + byte, 0);
  record[record.length - 1] = (-sum) & 0xff;
  return `:${record.toString("hex").toUpperCase()}`;
}

function emitHex(memory, startRecords) {
  const addresses = [...memory.keys()].sort((left, right) => left - right);
  const lines = [];
  let currentUpper = -1;
  let index = 0;

  while (index < addresses.length) {
    const start = addresses[index];
    const upper = Math.floor(start / 0x10000);
    if (upper !== currentUpper) {
      const upperData = Buffer.alloc(2);
      upperData.writeUInt16BE(upper, 0);
      lines.push(makeRecord(0x04, 0, upperData));
      currentUpper = upper;
    }

    const bytes = [];
    let expected = start;
    while (index < addresses.length && bytes.length < HEX_RECORD_SIZE &&
           addresses[index] === expected &&
           Math.floor(addresses[index] / 0x10000) === upper) {
      bytes.push(memory.get(addresses[index]));
      expected++;
      index++;
    }
    lines.push(makeRecord(0x00, start & 0xffff, Buffer.from(bytes)));
  }

  for (const record of startRecords) {
    lines.push(makeRecord(record.type, 0, record.data));
  }
  lines.push(makeRecord(0x01, 0, Buffer.alloc(0)));
  return `${lines.join("\n")}\n`;
}

function packageFirmware(options) {
  const firmwareText = fs.readFileSync(options.firmware, "utf8");
  const script = fs.readFileSync(options.script);
  const archive = createArchive(script);
  const { memory, startRecords } = parseHex(firmwareText);

  for (const address of memory.keys()) {
    if (address < FLASH_BASE || address >= LUA_ARCHIVE_ADDRESS) {
      throw new Error(
        `Firmware address 0x${address.toString(16).toUpperCase()} is outside ` +
        "the reserved application region."
      );
    }
  }
  for (let index = 0; index < archive.length; index++) {
    memory.set(LUA_ARCHIVE_ADDRESS + index, archive[index]);
  }

  fs.mkdirSync(path.dirname(path.resolve(options.output)), { recursive: true });
  fs.writeFileSync(options.output, emitHex(memory, startRecords), "ascii");
  if (options.archive) {
    fs.mkdirSync(path.dirname(path.resolve(options.archive)), { recursive: true });
    fs.writeFileSync(options.archive, archive);
  }

  return {
    firmwareBytes: [...memory.keys()].filter(address => address < LUA_ARCHIVE_ADDRESS).length,
    scriptBytes: script.length,
    archiveBytes: archive.length,
    scriptCrc32: archive.readUInt32LE(16),
    archiveCrc32: archive.readUInt32LE(20)
  };
}

try {
  const options = parseArguments(process.argv.slice(2));
  const result = packageFirmware(options);
  console.log(`Firmware bytes: ${result.firmwareBytes}`);
  console.log(`Lua source bytes: ${result.scriptBytes}`);
  console.log(`Archive bytes: ${result.archiveBytes}`);
  console.log(`Script CRC-32: ${result.scriptCrc32.toString(16).padStart(8, "0")}`);
  console.log(`Archive CRC-32: ${result.archiveCrc32.toString(16).padStart(8, "0")}`);
  console.log(
    `Archive address: 0x${LUA_ARCHIVE_ADDRESS.toString(16).toUpperCase().padStart(8, "0")}`
  );
  console.log(`Combined HEX: ${options.output}`);
} catch (error) {
  usage();
  console.error(error.message);
  process.exitCode = 1;
}
