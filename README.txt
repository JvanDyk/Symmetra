To test locally right now

# 1. Install Firebase CLI (if needed)
npm install -g firebase-tools

# 2. Build the function
cd functions && npm run build && cd ..

# 3. Start all emulators
firebase emulators:start

# 4. In a second terminal, seed the data
cd seed && npm run seed

# 5. Open http://localhost:5000/engines
#    → Create an account → click Download → file downloads via signed URL