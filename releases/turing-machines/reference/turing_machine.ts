import { serve, Router } from '@symmetra/http';

// Turing Machine: 5-state 2-symbol busy beaver
// Compiles to GPU via Symmetra engine
// Tests actual batching and warp coherence

interface TMState {
    state: number;
    tape: number[];
    position: number;
    steps: number;
}

interface TMTransition {
    writeSymbol: number;
    moveDir: number;  // -1 = left, +1 = right
    nextState: number;
}

// Busy beaver 5-state transition table
function getTransition(state: number, symbol: number): TMTransition {
    const table: TMTransition[][] = [
        // State 0
        [
            { writeSymbol: 1, moveDir: 1, nextState: 1 },   // read 0
            { writeSymbol: 1, moveDir: -1, nextState: 1 }   // read 1
        ],
        // State 1
        [
            { writeSymbol: 1, moveDir: 1, nextState: 2 },
            { writeSymbol: 1, moveDir: -1, nextState: 3 }
        ],
        // State 2
        [
            { writeSymbol: 1, moveDir: -1, nextState: 3 },
            { writeSymbol: 0, moveDir: -1, nextState: 0 }
        ],
        // State 3
        [
            { writeSymbol: 1, moveDir: -1, nextState: 4 },
            { writeSymbol: 0, moveDir: -1, nextState: 2 }
        ],
        // State 4
        [
            { writeSymbol: 1, moveDir: 1, nextState: 0 },
            { writeSymbol: 0, moveDir: 1, nextState: 4 }
        ]
    ];
    return table[state][symbol];
}

// Single Turing machine step
function step(tm: TMState): void {
    if (tm.position < 0 || tm.position >= tm.tape.length) {
        tm.state = -1;  // Halt at boundary
        return;
    }

    const symbol = tm.tape[tm.position];
    const trans = getTransition(tm.state, symbol);

    tm.tape[tm.position] = trans.writeSymbol;
    tm.position += trans.moveDir;
    tm.state = trans.nextState;
    tm.steps++;

    // Prevent infinite execution
    if (tm.steps > 5000) {
        tm.state = -1;  // Halt
    }
}

// Run Turing machine for N steps
function runMachine(tm: TMState, maxSteps: number): void {
    while (tm.state !== -1 && tm.steps < maxSteps) {
        step(tm);
    }
}

// Count active cells (ones on tape)
function countOnes(tape: number[]): number {
    let count = 0;
    for (let i = 0; i < tape.length; i++) {
        if (tape[i] === 1) count++;
    }
    return count;
}

// Initialize tape with zeros
function initTape(size: number): number[] {
    const tape: number[] = [];
    for (let i = 0; i < size; i++) {
        tape[i] = 0;
    }
    return tape;
}

const app = new Router();

// Turing machine benchmark endpoint
app.get('/tm', (ctx) => {
    const TAPE_SIZE = 128;
    const MAX_STEPS = 5000;
    const NUM_MACHINES = 32;  // Batch size

    const startTime = Date.now();

    // Run 32 Turing machines (all execute same code path)
    const machines: TMState[] = [];
    for (let i = 0; i < NUM_MACHINES; i++) {
        machines.push({
            state: 0,
            tape: initTape(TAPE_SIZE),
            position: TAPE_SIZE / 2,
            steps: 0
        });
    }

    // Execute all machines together
    for (let m = 0; m < NUM_MACHINES; m++) {
        runMachine(machines[m], MAX_STEPS);
    }

    const endTime = Date.now();
    const elapsed = endTime - startTime;

    // Compute statistics
    let totalSteps = 0;
    let totalOnes = 0;
    let halted = 0;

    for (let m = 0; m < NUM_MACHINES; m++) {
        totalSteps += machines[m].steps;
        totalOnes += countOnes(machines[m].tape);
        if (machines[m].state === -1) halted++;
    }

    const avgSteps = totalSteps / NUM_MACHINES;
    const stepsPerMs = elapsed > 0 ? totalSteps / elapsed : 0;

    ctx.json({
        benchmark: "Turing Machine (5-state busy beaver)",
        batchSize: NUM_MACHINES,
        tapeSize: TAPE_SIZE,
        maxStepsPerMachine: MAX_STEPS,
        executionTimeMs: elapsed,
        totalStepsExecuted: totalSteps,
        avgStepsPerMachine: Math.floor(avgSteps),
        totalOnesGenerated: totalOnes,
        machinesHalted: halted,
        machinesRunning: NUM_MACHINES - halted,
        throughputStepsPerMs: stepsPerMs,
        note: "All 32 machines execute same code path - ideal warp coherence"
    });
});

serve(app, { port: 8080 });
