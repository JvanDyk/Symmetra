# Website Tutorials & Getting Started Guide

This document outlines what should appear on the Symmetra website for tutorials, examples, and getting started content.

---

## Website Structure for Learning Path

```
website/
├── / (homepage)
│   ├── Hero: "GPU-Accelerated JavaScript"
│   ├── Why Symmetra (4 key benefits)
│   ├── Quick start (2 min overview)
│   └── CTA: "Start Learning" → /learn
│
├── /learn (Learning hub)
│   ├── Quick Start (5 min)
│   ├── Tutorials (4 comprehensive guides)
│   ├── Examples (side-by-side Node.js vs Symmetra)
│   └── FAQ
│
├── /learn/quick-start
│   └── "5 Minute Quick Start"
│
├── /learn/tutorials
│   ├── Tutorial 1: HTTP API
│   ├── Tutorial 2: CRUD Application
│   ├── Tutorial 3: Data Processing
│   └── Tutorial 4: Real-World Service
│
├── /learn/examples
│   ├── Side-by-side comparisons
│   ├── Node.js vs Symmetra
│   └── Performance benchmarks
│
└── /learn/faq
    └── Common questions
```

---

## 1. Quick Start (5 Minutes)

**Location**: `/learn/quick-start` or homepage section

**Content** (inline on page):

```markdown
# 5 Minute Quick Start

Start with Symmetra right now. Same code as Node.js, 50x faster.

## Step 1: Create a File

Create `server.ts`:

    import { serve, Router } from '@symmetra/http';

    const app = new Router();
    app.get('/hello', ctx => ctx.json({ message: 'Hello GPU!' }));
    serve(app, { port: 8080 });

## Step 2: Install Symmetra

    brew install symmetra  # or: winget install symmetra

## Step 3: Run It

    # On GPU
    sym build server.ts --with-spm
    sym run dist/server.cuda

    # On Node.js (same code!)
    npm install express uuid
    node server.ts

## Step 4: Test

    curl http://localhost:8080/hello

That's it! Same code, different runtime.
```

---

## 2. Tutorial 1: HTTP API (10 Minutes)

**Location**: `/learn/tutorials/http-api`

**Source**: `examples/1-http-api/README.md` (copy content to website)

**What users will build**:
- REST API with user management
- UUID generation
- Data hashing (crypto)
- JSON responses

**Flow**:
1. Create project
2. Create server.ts (copy-paste code)
3. Create spm.json and package.json
4. Run on Symmetra
5. Run on Node.js (same code!)
6. Test with curl
7. Compare performance

**Key learning**: "Same code works on both runtimes"

---

## 3. Tutorial 2: CRUD Application (20 Minutes)

**Location**: `/learn/tutorials/crud-app`

**Source**: `examples/2-crud-app/README.md` (copy content to website)

**What users will build**:
- Full CRUD API (Create, Read, Update, Delete)
- Filtering and searching
- Data analytics
- Price tracking
- Crypto hashing

**Features**:
- 10 endpoints
- Complex queries
- Statistics aggregation
- Data validation

**Key learning**: "Build real applications with Symmetra"

---

## 4. Side-by-Side Examples

**Location**: `/learn/examples`

**Source**: `NODE_VS_SYMMETRA_EXAMPLES.md` (copy content to website)

**Show 4 examples with code**:

### Example 1: HTTP API
- Code (identical)
- Run on Node.js
- Run on Symmetra
- Performance comparison (5x speedup)

### Example 2: Data Processing
- Process 10K items
- Filter, map, group, aggregate
- Node.js: 50ms
- Symmetra: 1ms (**50x faster**)

### Example 3: Crypto Operations
- Hash, HMAC, random generation
- Node.js: 50µs per hash
- Symmetra: 1µs per hash (**50x faster**)

### Example 4: Real-World API
- Combines all operations
- Users + crypto + analytics
- Shows practical use case

**Format**: Side-by-side code blocks
- Left: Node.js
- Right: Symmetra (same code!)
- Bottom: Performance table

---

## 5. Getting Started Scenarios

**Location**: `/learn/getting-started` (optional section)

Create pages for different user types:

### Scenario 1: "I know Node.js"
```
You already know how to build with Node.js.
Symmetra is identical - just faster.

1. Use same `import` statements
2. Use same npm packages
3. Get GPU acceleration automatically
4. No code changes needed

See Tutorial 1: HTTP API
```

### Scenario 2: "I want to learn JavaScript"
```
Use Node.js first (more resources available).
When you're comfortable, run the same code on Symmetra
to see 50x performance boost.

Start with Node.js:
- More tutorials online
- Easier debugging
- Huge community

Then try Symmetra:
- Same knowledge applies
- 50x faster
- GPU power

See Tutorial 1: HTTP API
```

### Scenario 3: "I need performance"
```
Symmetra is 50x faster for CPU-intensive work:
- Crypto: 50x faster
- Data processing: 10-50x faster
- Batch operations: unlimited parallelism

See Example 2: Data Processing
See Example 3: Crypto Operations
```

---

## 6. FAQ Section

**Location**: `/learn/faq`

```markdown
## Frequently Asked Questions

### Q: Do I need to learn new syntax?
A: No! Use Node.js syntax. If you know JavaScript, you know Symmetra.

### Q: What if I have existing Node.js code?
A: It works unchanged. Just replace `node` with `sym`.

### Q: Which npm packages work?
A: 2M+ npm packages work. Anything that's pure JavaScript.
[See Compatibility Matrix](../SPM_COMPAT_REFERENCE.md)

### Q: How much faster is Symmetra?
A: Crypto: 50x faster
   Data processing: 10-50x faster
   Network I/O: same (bottleneck is network)

### Q: What if I need something Node.js doesn't support?
A: File I/O, process spawning, networking not supported on GPU.
   For those, use Node.js or submit as feature request.

### Q: Can I mix Node.js and Symmetra code?
A: Yes! Same codebase can run on both.
   Choose runtime based on performance needs.

### Q: How do I debug?
A: Same tools as Node.js. Add `sym --debug` for debugging.

### Q: What about TypeScript?
A: Supported! Use .ts files directly.

### Q: Is there a learning curve?
A: No. If you know Node.js, you know Symmetra.

### Q: What's the catch?
A: GPU only runs compute code (crypto, data processing).
   Network I/O and file operations use normal CPU paths.
   That's expected and fine — GPU is for the compute bottleneck.
```

---

## 7. Resources & Links

**Location**: `/learn/resources`

```markdown
## Learning Resources

### Official Documentation
- [Symmetra Package Manager Guide](../SPM_USER_GUIDE.md)
- [API Compatibility Reference](../SPM_COMPAT_REFERENCE.md)
- [Installation Guide](../README.md)

### Tutorials
- [Tutorial 1: HTTP API](./tutorials/http-api)
- [Tutorial 2: CRUD Application](./tutorials/crud-app)
- [Side-by-Side Examples](./examples)

### External Resources
- [Node.js Documentation](https://nodejs.org/docs/)
- [JavaScript Guide](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide)
- [CUDA Documentation](https://docs.nvidia.com/cuda/)

### Community
- GitHub Discussions
- Discord Channel
- Stack Overflow tag: `symmetra-gpu`

### System Requirements
- NVIDIA GPU (RTX 2060+, A4000+, H100+)
- 50GB+ VRAM
- Linux or Windows
```

---

## 8. Interactive Tutorial (Advanced)

**Location**: `/learn/interactive`

Create an **interactive code sandbox** where users can:
- Write code directly in browser
- See output in real-time
- Switch between Node.js and Symmetra
- See performance difference

```html
<iframe src="https://symmetra-replit.com?tutorial=http-api"></iframe>
```

---

## 9. Comparison Table

**Location**: `/learn/compare` or `/why-symmetra`

Create visual comparison:

| Feature | Node.js | Symmetra |
|---------|---------|----------|
| **Learning curve** | ✅ Moderate | ✅ Zero (same as Node.js) |
| **npm packages** | ✅ All | ✅ All |
| **Code changes** | N/A | ✅ None required |
| **Performance** | Baseline | ✅ 10-50x faster |
| **Crypto** | CPU | ✅ GPU (50x) |
| **Data processing** | CPU | ✅ GPU (10-50x) |
| **Real-time analytics** | CPU | ✅ GPU (20-50x) |
| **Network I/O** | ✅ Good | ✅ Good (not GPU) |
| **File I/O** | ✅ Full | ⚠️ Limited |
| **Process spawning** | ✅ Yes | ❌ No (GPU doesn't support) |
| **Cost per 1M RPS** | $15-20K | ✅ $7-12K |
| **Community** | ✅ Huge | 🚀 Growing |

---

## 10. Homepage "Getting Started" Section

**Location**: Homepage (above fold)

```markdown
## Get Started in 5 Minutes

Choose how to learn:

### 👨‍💻 I know Node.js
→ [Quick Start](./learn/quick-start)
"Your JavaScript knowledge transfers 1:1. Run the same code."

### 📚 I'm new to JavaScript
→ [Tutorial 1: HTTP API](./learn/tutorials/http-api)
"Step-by-step guide. Learn JavaScript while building real things."

### ⚡ I want performance
→ [Examples: Node.js vs Symmetra](./learn/examples)
"See 50x performance gain with identical code."

### 🔍 I have questions
→ [FAQ](./learn/faq)
"Answers to common questions."
```

---

## 11. Social Proof / Case Studies Section (Future)

**Location**: `/why-symmetra/case-studies`

Once we have real users:
- Company X: "Reduced crypto bottleneck by 50x"
- Company Y: "Processed 1TB data in real-time with Symmetra"
- Company Z: "Saved $100K/year on infrastructure"

---

## 12. "Try Without Installing" Option

**Location**: Interactive sandbox on website

```markdown
## Try Symmetra Online

Write code and run it directly in your browser:

<iframe src="https://symmetra-playground.dev">
  # Try this example:
  import { serve, Router } from '@symmetra/http';
  const app = new Router();
  app.get('/hello', ctx => ctx.json({ message: 'Hello!' }));
  serve(app, { port: 8080 });
</iframe>

No installation required. See live results.
```

---

## Content Checklist for Website

- [ ] **Quick Start**: Copy `examples/1-http-api/README.md`
- [ ] **Tutorial 1**: Copy `examples/1-http-api/` content
- [ ] **Tutorial 2**: Copy `examples/2-crud-app/` content
- [ ] **Examples**: Copy `NODE_VS_SYMMETRA_EXAMPLES.md`
- [ ] **FAQ**: Write 10-15 common questions
- [ ] **Resources**: Link to all docs
- [ ] **Comparison**: Create visual table
- [ ] **Getting Started**: Map for different user types
- [ ] **Scenarios**: "I know Node.js", "I'm new", "I need performance"
- [ ] **Interactive Sandbox**: Optional (can do later)

---

## URL Structure Recommendation

```
/                          (homepage)
/install                   (installation)
/learn                     (hub for all learning)
/learn/quick-start         (5 min intro)
/learn/tutorials           (all tutorials)
/learn/tutorials/http-api  (tutorial 1)
/learn/tutorials/crud-app  (tutorial 2)
/learn/examples            (side-by-side code)
/learn/faq                 (Q&A)
/learn/resources           (external links)
/why-symmetra              (differentiation)
/why-symmetra/comparison   (vs Node.js/Bun/Deno)
/docs                      (detailed docs)
/docs/api                  (API reference)
/docs/spm                  (SPM guide)
/docs/compatibility        (which APIs work)
```

---

## Content Style Guide

For all tutorials:
✅ **Do**:
- Use copy-paste ready code
- Include `package.json` and `spm.json`
- Show exact commands to run
- Include expected output
- Explain what's happening
- Show performance comparison
- Target complete beginners

❌ **Don't**:
- Assume prior knowledge
- Use jargon without explaining
- Show incomplete code examples
- Make it sound hard or scary
- Bury the "same code" message
- Forget Node.js equivalents

---

## Key Messages to Reinforce

On every page, make these clear:

1. **"Same code as Node.js"** — No syntax differences
2. **"50x faster"** — GPU acceleration is real
3. **"Works with npm packages"** — Use your favorite libraries
4. **"No code changes"** — Write for Node.js, run on GPU
5. **"Easy to learn"** — If you know Node.js, you're done

---

## Next Steps

1. **Phase 1**: Copy tutorial content to website
2. **Phase 2**: Create FAQ and resources pages
3. **Phase 3**: Build interactive sandbox
4. **Phase 4**: Add case studies and testimonials

All content is ready in this repo. Just copy and adapt for website layout.
