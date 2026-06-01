'use strict';
const { randomUUID } = require('crypto');
/**
 * Symmetra emulator seed
 *
 * Seeds Firestore with the engine catalog and uploads placeholder binaries to
 * the Storage emulator so the full download flow can be tested locally.
 *
 * Usage:
 *   1. Start emulators: firebase emulators:start
 *   2. In another terminal: cd seed && npm install && npm run seed
 */

// Point Admin SDK at local emulators
process.env.FIRESTORE_EMULATOR_HOST       = '127.0.0.1:8080';
process.env.FIREBASE_STORAGE_EMULATOR_HOST = '127.0.0.1:9199';

const admin = require('firebase-admin');

admin.initializeApp({
  projectId:     'symmetra-38ad8',
  storageBucket: 'symmetra-38ad8.firebasestorage.app',
  // Emulator ignores credentials — dummy value is fine
  credential:    admin.credential.applicationDefault(),
});

const db      = admin.firestore();
const storage = admin.storage().bucket();

// ─────────────────────────────────────────────────────────────────────────────
// Engine catalog
// Only tm-simple and tm-moderate are available:true for now.
// ─────────────────────────────────────────────────────────────────────────────
const engines = [
  // ── Available (free tier) ────────────────────────────────────────────────
  {
    id:               'tm-simple',
    name:             'Simple',
    displayName:      'Simple Reference Machine',
    identifier:       'symmetra:tm-simple:1.0.0',
    version:          '1.0.0',
    description:      '3-state Turing machine with 40-cell tape. The classic reference implementation — ideal for learning GPU computation fundamentals and validating your environment.',
    states:           3,
    tapeSize:         40,
    throughput:       '100K',
    throughputRaw:    100_000,
    latency:          '10µs',
    gpuUtilization:   '50%',
    available:        true,
    tier:             'free',
    storagePathWindows: 'engines/tm-simple/v1.0.0/sym-simple-v1.0.0-windows.zip',
    storagePathLinux:   'engines/tm-simple/v1.0.0/sym-simple-v1.0.0-linux.tar.gz',
    createdAt:        admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:               'tm-moderate',
    name:             'Moderate',
    displayName:      'Moderate State Machine',
    identifier:       'symmetra:tm-moderate:1.0.0',
    version:          '1.0.0',
    description:      '4-state Turing machine with 64-cell tape. Bridges reference and production complexity — good for benchmarking moderate workloads.',
    states:           4,
    tapeSize:         64,
    throughput:       '50K',
    throughputRaw:    50_000,
    latency:          '20µs',
    gpuUtilization:   '65%',
    available:        true,
    tier:             'free',
    storagePathWindows: 'engines/tm-moderate/v1.0.0/sym-moderate-v1.0.0-windows.zip',
    storagePathLinux:   'engines/tm-moderate/v1.0.0/sym-moderate-v1.0.0-linux.tar.gz',
    createdAt:        admin.firestore.FieldValue.serverTimestamp(),
  },

  // ── Coming soon ───────────────────────────────────────────────────────────
  {
    id:             'tm-reference',
    name:           'Reference',
    displayName:    'Reference Busy Beaver',
    identifier:     'symmetra:tm-reference:1.0.0',
    version:        '1.0.0',
    description:    '5-state busy beaver with 128-cell tape. Complex lookup-table transitions — tests real-world GPU divergence.',
    states:         5,
    tapeSize:       128,
    throughput:     '10K',
    throughputRaw:  10_000,
    latency:        '100µs',
    gpuUtilization: '20%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-superfast',
    name:           'Superfast',
    displayName:    'Superfast 1-State Machine',
    identifier:     'symmetra:tm-superfast:1.0.0',
    version:        '1.0.0',
    description:    'Single-state machine with 20-cell tape. Minimal state checking, maximum sustained throughput.',
    states:         1,
    tapeSize:       20,
    throughput:     '232K',
    throughputRaw:  232_000,
    latency:        '4µs',
    gpuUtilization: '70%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-balanced',
    name:           'Balanced',
    displayName:    'Balanced State Machine',
    identifier:     'symmetra:tm-balanced:1.0.0',
    version:        '1.0.0',
    description:    '2-state machine with 25-cell tape. Practical dual-state logic at production throughput.',
    states:         2,
    tapeSize:       25,
    throughput:     '1M',
    throughputRaw:  1_000_000,
    latency:        '1µs',
    gpuUtilization: '72%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-ultra',
    name:           'Ultra',
    displayName:    'Ultra Minimal Tape',
    identifier:     'symmetra:tm-ultra:1.0.0',
    version:        '1.0.0',
    description:    'Zero-state machine with 10-cell tape. Pure bit-flip operations — measures raw memory bandwidth.',
    states:         0,
    tapeSize:       10,
    throughput:     '2.5M',
    throughputRaw:  2_500_000,
    latency:        '0.4µs',
    gpuUtilization: '60%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-extreme',
    name:           'Extreme',
    displayName:    'Extreme Compute Machine',
    identifier:     'symmetra:tm-extreme:1.0.0',
    version:        '1.0.0',
    description:    '1-state machine with 12-cell tape. High-throughput sweet spot between memory and compute.',
    states:         1,
    tapeSize:       12,
    throughput:     '8M',
    throughputRaw:  8_000_000,
    latency:        '0.12µs',
    gpuUtilization: '85%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-hyper',
    name:           'Hyper',
    displayName:    'Hyper High-Speed Machine',
    identifier:     'symmetra:tm-hyper:1.0.0',
    version:        '1.0.0',
    description:    '1-state machine with 15-cell tape. Near-peak GPU pipeline saturation with practical program structure.',
    states:         1,
    tapeSize:       15,
    throughput:     '16M',
    throughputRaw:  16_000_000,
    latency:        '0.06µs',
    gpuUtilization: '90%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
  {
    id:             'tm-lightning',
    name:           'Lightning',
    displayName:    'Lightning Pure Compute',
    identifier:     'symmetra:tm-lightning:1.0.0',
    version:        '1.0.0',
    description:    'Zero-state pure arithmetic kernel. 100 iterations of fused multiply-add per thread — maximum GPU instruction throughput with zero memory ops.',
    states:         0,
    tapeSize:       0,
    throughput:     '32.5M',
    throughputRaw:  32_500_000,
    latency:        '0.03µs',
    gpuUtilization: '95%',
    available:      false,
    tier:           'coming-soon',
    storagePathWindows: null,
    storagePathLinux:   null,
    createdAt:      admin.firestore.FieldValue.serverTimestamp(),
  },
];

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder file content for emulator storage
// Replace with real binaries when ready to release.
// ─────────────────────────────────────────────────────────────────────────────
const placeholderFiles = [
  {
    path:    'engines/tm-simple/v1.0.0/sym-simple-v1.0.0-windows.zip',
    content: 'SYMMETRA ENGINE: Simple v1.0.0 (Windows)\n[Replace with actual sym.exe bundle]\n',
    type:    'application/zip',
  },
  {
    path:    'engines/tm-simple/v1.0.0/sym-simple-v1.0.0-linux.tar.gz',
    content: 'SYMMETRA ENGINE: Simple v1.0.0 (Linux)\n[Replace with actual sym binary bundle]\n',
    type:    'application/gzip',
  },
  {
    path:    'engines/tm-moderate/v1.0.0/sym-moderate-v1.0.0-windows.zip',
    content: 'SYMMETRA ENGINE: Moderate v1.0.0 (Windows)\n[Replace with actual sym.exe bundle]\n',
    type:    'application/zip',
  },
  {
    path:    'engines/tm-moderate/v1.0.0/sym-moderate-v1.0.0-linux.tar.gz',
    content: 'SYMMETRA ENGINE: Moderate v1.0.0 (Linux)\n[Replace with actual sym binary bundle]\n',
    type:    'application/gzip',
  },
];

// ─────────────────────────────────────────────────────────────────────────────
async function main() {
  console.log('\n🌱  Symmetra emulator seed\n');

  // ── Firestore ──────────────────────────────────────────────────────────────
  console.log('Writing engine catalog to Firestore...');
  const batch = db.batch();
  for (const engine of engines) {
    const { id, ...data } = engine;
    batch.set(db.collection('engines').doc(id), data);
  }
  await batch.commit();
  console.log(`  ✓ ${engines.length} engines written`);

  // ── Storage ────────────────────────────────────────────────────────────────
  console.log('\nUploading placeholder files to Storage emulator...');
  for (const f of placeholderFiles) {
    const file = storage.file(f.path);
    const token = randomUUID(); // download token — bypasses storage rules
    await file.save(Buffer.from(f.content, 'utf-8'), {
      contentType: f.type,
      metadata: {
        metadata: {
          firebaseStorageDownloadTokens: token,
          placeholder: 'true',
          seeded: new Date().toISOString(),
        },
      },
    });
    console.log(`  ✓ ${f.path}  (token: ${token})`);
  }

  console.log('\n✅  Seed complete!\n');
  console.log('  Firestore UI:  http://127.0.0.1:4000/firestore');
  console.log('  Storage UI:    http://127.0.0.1:4000/storage');
  console.log('  Auth UI:       http://127.0.0.1:4000/auth');
  console.log('  Website:       http://127.0.0.1:5000/engines\n');

  process.exit(0);
}

main().catch((err) => {
  console.error('\n❌  Seed failed:', err.message);
  process.exit(1);
});
