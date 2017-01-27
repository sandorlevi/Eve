import {Program} from "../src/runtime/dsl";
import {verify} from "./util";
import * as test from "tape";

test("find a record and generate a record as a result", (assert) => {

  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    find({foo: "bar"});
    return [
      record({zomg: "baz"})
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "foo", "bar"]
  ], [
    [2, "zomg", "baz", 1]
  ])

  assert.end();
});


test("> filters numbers", (assert) => {

  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    let a = find();
    let b = find();
    a.age > b.age;
    return [
      record({age1: a.age, age2: b.age})
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "tag", "person"],
    [1, "age", 7],
    [2, "tag", "person"],
    [2, "age", 41],
    [3, "tag", "person"],
    [3, "age", 3],
  ], [
    [4, "age1", 41, 1],
    [4, "age2", 7, 1],
    [5, "age1", 41, 1],
    [5, "age2", 3, 1],
    [6, "age1", 7, 1],
    [6, "age2", 3, 1],
  ])

  assert.end();
});


test("simple addition", (assert) => {

  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    let a = find("person");
    let b = find("person");
    a.age > b.age;
    let result = a.age + b.age;
    return [
      record({age1: a.age, age2: b.age, result})
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "tag", "person"],
    [1, "age", 7],
    [2, "tag", "person"],
    [2, "age", 41],
    [3, "tag", "person"],
    [3, "age", 3],
  ], [
    [4, "age1", 41, 1],
    [4, "age2", 7, 1],
    [4, "result", 48, 1],
    [5, "age1", 41, 1],
    [5, "age2", 3, 1],
    [5, "result", 44, 1],
    [6, "age1", 7, 1],
    [6, "age2", 3, 1],
    [6, "result", 10, 1],
  ])

  assert.end();
});

test("simple recursion", (assert) => {
  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    let {number} = find();
    9 > number;
    let result = number + 1;
    return [
      record({number: result})
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "number", 1],
  ], [
    [2, "number", 2, 1],
    [3, "number", 3, 2],
    [4, "number", 4, 3],
    [5, "number", 5, 4],
    [6, "number", 6, 5],
    [7, "number", 7, 6],
    [8, "number", 8, 7],
    [9, "number", 9, 8],
  ]);

  assert.end();
});

test("test addition operator", (assert) => {

  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    let joof = find({foo: "bar"});
    return [
      joof.add("name", "JOOF")
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "foo", "bar"]
  ], [
    [1, "name", "JOOF", 1]
  ])

  assert.end();
});

test("transitive closure", (assert) => {
  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("Every edge is the beginning of a path.", (find:any, record:any, lib:any) => {
    let from = find();
    return [
      from.add("path", from.edge)
    ];
  });

  prog.block("Jump from node to node building the path.", (find:any, record:any, lib:any) => {
    let from = find();
    let intermediate = find();
    from.edge == intermediate;
    let to = intermediate.path;

    intermediate.path;
    return [
      from.add("path", to)
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  verify(assert, prog, [
    [1, "edge", 2],
    [2, "edge", 3],
    [3, "edge", 4],
    [4, "edge", 1],
  ], [
    [1, "path", 2, 1],
    [2, "path", 3, 1],
    [3, "path", 4, 1],
    [4, "path", 1, 1],

    [1, "path", 3, 2],
    [2, "path", 4, 2],
    [3, "path", 1, 2],
    [4, "path", 2, 2],

    [1, "path", 4, 3],
    [2, "path", 1, 3],
    [3, "path", 2, 3],
    [4, "path", 3, 3],

    [1, "path", 1, 4],
    [2, "path", 2, 4],
    [3, "path", 3, 4],
    [4, "path", 4, 4],

    [1, "path", 2, 5],
    [2, "path", 3, 5],
    [3, "path", 4, 5],
    [4, "path", 1, 5]
  ]);

  // Kick the legs out from under the cycle.

  verify(assert, prog, [
    [4, "edge", 1, 0, -1]
  ], [
    [4, "path", 1, 1, -1],

    [4, "path", 2, 2, -1],
    [3, "path", 1, 2, -1],
    [2, "path", 1, 2, -1],
    [1, "path", 1, 2, -1],

    [4, "path", 3, 3, -1],
    [3, "path", 2, 3, -1],
    [2, "path", 2, 3, -1],
    [1, "path", 2, 3, -1],

    [4, "path", 4,  4, -1],

    [4, "path", 1,  5, -1],
  ]);


  assert.end();
});

test("removal", (assert) => {

  // -----------------------------------------------------
  // program
  // -----------------------------------------------------

  let prog = new Program("test");
  prog.block("simple block", (find:any, record:any, lib:any) => {
    find({foo: "bar"});
    return [
      record({zomg: "baz"})
    ]
  });

  // -----------------------------------------------------
  // verification
  // -----------------------------------------------------

  // trust, but
  verify(assert, prog, [
    [1, "foo", "bar"]
  ], [
    [2, "zomg", "baz", 1]
  ]);

  verify(assert, prog, [
    [1, "foo", "bar", 0, -1]
  ], [
    [2, "zomg", "baz", 1, -1]
  ], 1);

  assert.end();
});
