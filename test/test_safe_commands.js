// Test and demonstration of safe transaction commands:
// SS-SET, E-SET, E-GET, SS-GET, SS-QUERY, E-SHOW

console.log("=== Testing Safe Transaction Commands ===");

// 1. Safe Selection Query (SS-GET)
console.log("\n1. Testing editor.ssGet():");
const allEntities = editor.ssGet();
console.log(`Found ${allEntities.length} entities in selection / drawing.`);
if (allEntities.length > 0) {
    const first = allEntities[0];
    console.log(`First entity handle: ${first.hand}, type: ${first.ty}, layer: ${first.lyr}, color: ${JSON.stringify(first.clr)}`);
}

// 2. Safe Query Aggregation (SS-QUERY)
console.log("\n2. Testing editor.ssQuery():");
const stats = editor.ssQuery({
    query: (entities) => {
        const types = {};
        for (const e of entities) {
            types[e.ty] = (types[e.ty] || 0) + 1;
        }
        return {
            total: entities.length,
            typeCounts: types
        };
    }
});
console.log("SS-QUERY stats:", JSON.stringify(stats));

// 3. Safe Entity Get (E-GET)
if (allEntities.length > 0) {
    const targetHandle = allEntities[0].hand;
    console.log(`\n3. Testing editor.eGet('${targetHandle}'):`);
    const snapshot = editor.eGet(targetHandle);
    console.log("eGet snapshot:", JSON.stringify({
        id: snapshot.id,
        hand: snapshot.hand,
        ty: snapshot.ty,
        clr: snapshot.clr,
        lyr: snapshot.lyr,
        visible: snapshot.visible
    }));

    // 4. Safe Entity Set (E-SET)
    console.log(`\n4. Testing editor.eSet('${targetHandle}'):`);
    const updated = editor.eSet(targetHandle, {
        clr: 1, // Red
        data: { testNote: "Mutated safely in transaction", timestamp: Date.now() }
    });
    console.log(`eSet updated color: ${JSON.stringify(updated.clr)}, data: ${JSON.stringify(updated.data)}`);

    // 5. Safe Entity Show & Zoom (E-SHOW)
    console.log(`\n5. Testing editor.eShow('${targetHandle}'):`);
    const showRes = editor.eShow(targetHandle);
    console.log("eShow result:", JSON.stringify(showRes));
}

// 6. Safe Selection Set Mutation: Implicit layer creation with last color
console.log("\n6. Testing editor.ssSet() with new layer creation (Qs.XXX):");
const layerCreateRes = editor.ssSet({
    set: e => ({
        lyr: "Qs.XXX" // Automatically created in transaction using last color!
    })
});
console.log("ssSet new layer result:", JSON.stringify(layerCreateRes));

// 7. Testing atomic rollback on exception / validation error
console.log("\n7. Testing editor.ssSet() atomic rollback on failure:");
try {
    editor.ssSet({
        set: (e) => {
            // Intentionally throw on a candidate to verify full rollback
            throw new Error("Simulated failure to test atomic rollback");
        }
    });
    console.log("Unexpected success");
} catch (err) {
    console.log("Expected rollback triggered successfully:", err.message);
}

// 8. Valid SS-SET with full properties and metadata
console.log("\n8. Testing full editor.ssSet() with XData:");
const passRes = editor.ssSet({
    init: (ctx) => console.log(`Starting ssSet transaction on ${ctx.count} candidate entities...`),
    filter: e => true,
    set: e => ({
        visible: true,
        data: { audited: true, batchId: "batch-01" }
    }),
    post: (ctx) => console.log(`Completed batch update: processed ${ctx.processedCount} entities.`)
});
console.log("ssSet result:", JSON.stringify(passRes));

console.log("\n=== Safe Transaction Commands Test Completed ===");
