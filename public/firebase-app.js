// Firebase browser SDK — single init, emulator-aware.
// Import this as an ES module: import { auth, db, functions } from '/firebase-app.js'

import { initializeApp }             from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js';
import { getAuth, connectAuthEmulator }              from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js';
import { getFirestore, connectFirestoreEmulator }    from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-firestore.js';
import { getFunctions, connectFunctionsEmulator }    from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-functions.js';
import { getStorage, connectStorageEmulator }        from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-storage.js';

const firebaseConfig = {
  apiKey:            'AIzaSyADaEZc_Tt0T3MIDesUXArxCMgCJXfH02Y',
  authDomain:        'symmetra-38ad8.firebaseapp.com',
  projectId:         'symmetra-38ad8',
  storageBucket:     'symmetra-38ad8.firebasestorage.app',
  messagingSenderId: '395976371170',
  appId:             '1:395976371170:web:fac4388ebaca655a91a7bb',
  measurementId:     'G-0WF9JN248R',
};

const app       = initializeApp(firebaseConfig);
const auth      = getAuth(app);
const db        = getFirestore(app);
const functions = getFunctions(app);
const storage   = getStorage(app);

// Wire up emulators when running locally
const isLocal = location.hostname === 'localhost' || location.hostname === '127.0.0.1';
if (isLocal) {
  connectAuthEmulator(auth,          'http://127.0.0.1:9099',  { disableWarnings: true });
  connectFirestoreEmulator(db,       '127.0.0.1', 8080);
  connectFunctionsEmulator(functions,'127.0.0.1', 5001);
  connectStorageEmulator(storage,    '127.0.0.1', 9199);
}

export { app, auth, db, functions, storage, isLocal };
