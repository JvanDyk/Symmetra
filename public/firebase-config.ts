// Import the functions you need from the SDKs you need
import { initializeApp } from "firebase/app";
import { getAnalytics } from "firebase/analytics";
// TODO: Add SDKs for Firebase products that you want to use
// https://firebase.google.com/docs/web/setup#available-libraries

// Your web app's Firebase configuration
// For Firebase JS SDK v7.20.0 and later, measurementId is optional
const firebaseConfig = {
  apiKey: "AIzaSyADaEZc_Tt0T3MIDesUXArxCMgCJXfH02Y",
  authDomain: "symmetra-38ad8.firebaseapp.com",
  projectId: "symmetra-38ad8",
  storageBucket: "symmetra-38ad8.firebasestorage.app",
  messagingSenderId: "395976371170",
  appId: "1:395976371170:web:fac4388ebaca655a91a7bb",
  measurementId: "G-0WF9JN248R"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);