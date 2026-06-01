import { onCall, HttpsError } from 'firebase-functions/v2/https';
import { initializeApp }      from 'firebase-admin/app';
import { getFirestore }       from 'firebase-admin/firestore';
import { getStorage }         from 'firebase-admin/storage';

initializeApp();

// ─────────────────────────────────────────────────────────────────────────────
// getDownloadUrl
//
// Callable function that returns a short-lived signed URL for a Turing machine
// binary stored in Firebase Storage.  The caller must be authenticated; the
// engine must be marked available:true in Firestore.
//
// Request:  { engineId: string, platform: 'windows' | 'linux' }
// Response: { url: string, filename: string, engineName: string, version: string }
// ─────────────────────────────────────────────────────────────────────────────
export const getDownloadUrl = onCall(
  { region: 'us-central1' },
  async (request) => {
    // ── Auth check ──
    if (!request.auth) {
      throw new HttpsError(
        'unauthenticated',
        'You must be signed in to download engines.',
      );
    }

    const { engineId, platform } = request.data as {
      engineId: string;
      platform: string;
    };

    // ── Input validation ──
    const VALID_PLATFORMS = ['windows-zip','windows-msi','linux-deb','linux-rpm','linux-tar'];
    if (typeof engineId !== 'string' || !engineId.trim()) {
      throw new HttpsError('invalid-argument', 'engineId is required.');
    }
    if (!VALID_PLATFORMS.includes(platform)) {
      throw new HttpsError(
        'invalid-argument',
        `platform must be one of: ${VALID_PLATFORMS.join(', ')}.`,
      );
    }

    // ── Fetch engine metadata ──
    const db         = getFirestore();
    const engineSnap = await db.doc(`engines/${engineId}`).get();

    if (!engineSnap.exists) {
      throw new HttpsError('not-found', 'Engine not found in catalog.');
    }

    const engine = engineSnap.data()!;

    if (!engine.available) {
      throw new HttpsError(
        'permission-denied',
        'This engine is not yet available for download.',
      );
    }

    // ── Resolve storage path ──
    const pathMap: Record<string, string | null> = {
      'windows-zip': engine.storagePathWindowsZip ?? null,
      'windows-msi': engine.storagePathWindowsMsi ?? null,
      'linux-deb':   engine.storagePathLinuxDeb   ?? null,
      'linux-rpm':   engine.storagePathLinuxRpm   ?? null,
      'linux-tar':   engine.storagePathLinuxTar   ?? null,
    };
    const storagePath = pathMap[platform];

    if (!storagePath) {
      throw new HttpsError(
        'not-found',
        `No ${platform} package is available for this engine yet.`,
      );
    }

    // ── Check file exists + resolve download URL ──
    const bucket = getStorage().bucket();
    const file   = bucket.file(storagePath);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const f = file as any;

    const [exists] = await f.exists() as [boolean];
    if (!exists) {
      throw new HttpsError(
        'not-found',
        'Engine file not found in storage. Check back soon.',
      );
    }

    // The Storage emulator has no signing credentials, so getSignedUrl() throws.
    // In emulator mode, read the firebaseStorageDownloadTokens from file metadata
    // (set by the seed script) — download tokens bypass security rules.
    let downloadUrl: string;
    if (process.env.FUNCTIONS_EMULATOR) {
      const [meta]  = await f.getMetadata() as [Record<string, any>];
      const token   = meta?.metadata?.firebaseStorageDownloadTokens as string | undefined;
      const bucketName = bucket.name;
      downloadUrl =
        `http://127.0.0.1:9199/v0/b/${encodeURIComponent(bucketName)}` +
        `/o/${encodeURIComponent(storagePath)}?alt=media` +
        (token ? `&token=${token}` : '');
    } else {
      const [signed] = await f.getSignedUrl({
        action:  'read',
        expires: Date.now() + 60 * 60 * 1000,
      }) as [string];
      downloadUrl = signed;
    }

    // ── Log the download ──
    await db.collection('downloads').add({
      userId:        request.auth.uid,
      userEmail:     request.auth.token.email ?? null,
      engineId,
      engineName:    engine.name,
      engineVersion: engine.version,
      platform,
      timestamp:     new Date(),
    });

    const filename = storagePath.split('/').pop() ?? `${engineId}-${platform}`;

    return {
      url:        downloadUrl,
      filename,
      engineName: engine.name,
      version:    engine.version,
    };
  },
);
