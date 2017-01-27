import {Proposal, Change, ResolvedValue, createArray, createHash, IGNORE_REG, ID, EAVN, EAVNField, Register, Constraint, ALLOCATION_COUNT} from "./runtime";

//------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------

function isResolved(field:ResolvedValue): field is ID {
  return field !== undefined && field !== IGNORE_REG;
}

// This function sums the counts of a packed array of node,
// transcation, round, count (ntrc) up to the given transaction and
// round based on the partial order (t1 <= t2 && r1 <= r2)
function sumTimes(ntrcArray:number[], transaction:number, round:number) {
  if(!ntrcArray) return 0;
  let total = 0;
  for(let i = 0, len = ntrcArray.length; i < len; i += 4) {
    let t = ntrcArray[i + 1];
    let r = ntrcArray[i + 2];
    if(t <= transaction && r <= round) total += ntrcArray[i + 3];
  }
  return total;
}

//------------------------------------------------------------------------
// Indexes
//------------------------------------------------------------------------

export interface Index {
  insert(change:Change):void;
  propose(proposal:Proposal, e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):Proposal;
  resolveProposal(proposal:Proposal):any[][];
  get(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):EAVN[];
  check(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):boolean;
  checkMultiplicity(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):number;
}

export class ListIndex implements Index {
  changes: Change[] = createArray();
  insert(change:Change) {
     this.changes.push(change);
  }

  resolveProposal(proposal:Proposal) {
    return proposal.info;
  }

  propose(proposal:Proposal, e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number) {
    let final = createArray("indexProposeResults") as ID[][];
    let forFields:EAVNField[] = createArray("indexForFields");
    let seen = createHash();

    if(a === undefined) forFields.push("a");
    else if(v === undefined) forFields.push("v");
    else if(e === undefined) forFields.push("e");
    else if(n === undefined) forFields.push("n");

    for(let change of this.changes) {
      if((e === undefined || e === IGNORE_REG || e === change.e) &&
         (a === undefined || a === IGNORE_REG || a === change.a) &&
         (v === undefined || v === IGNORE_REG || v === change.v) &&
         (n === undefined || n === IGNORE_REG || n === change.n) &&
         (change.transaction <= transaction) &&
         (change.round <= round)) {
        let current = change[forFields[0]];
        if(!seen[current]) {
          seen[current] = true;
          final.push([current]);
        }
      }
    }

    proposal.cardinality = final.length;
    proposal.info = final;
    proposal.forFields = forFields;
    return proposal;
  }

  check(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):boolean {
    return this.checkMultiplicity(e, a, v, n, transaction, round) > 0;
  }

  checkMultiplicity(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):number {
    let count = 0;
    for(let change of this.changes) {
      if((e === undefined || e === IGNORE_REG || e === change.e) &&
         (a === undefined || a === IGNORE_REG || a === change.a) &&
         (v === undefined || v === IGNORE_REG || v === change.v) &&
         (n === undefined || n === IGNORE_REG || n === change.n) &&
         (change.transaction <= transaction) &&
         (change.round <= round)) {
        count += change.count;
      }
    }
    return count;
  }

  get(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):EAVN[] {
    let final = createArray() as EAVN[];
    for(let change of this.changes) {
      if((e === undefined || e === IGNORE_REG || e === change.e) &&
         (a === undefined || a === IGNORE_REG || a === change.a) &&
         (v === undefined || v === IGNORE_REG || v === change.v) &&
         (n === undefined || n === IGNORE_REG || n === change.n) &&
         (change.transaction <= transaction) &&
         (change.round <= round)) {
        final.push(new EAVN(change.e, change.a, change.v, change.n))
      }
    }
    return final;
  }
}

export class HashIndex implements Index {
  eavIndex = createHash();
  aveIndex = createHash();
  cardinality = 0;

  getOrCreateHash(parent:any, key:any) {
    let found = parent[key];
    if(!found) {
      found = parent[key] = createHash("hashLevel");
    }
    return found;
  }

  getOrCreateArray(parent:any, key:any) {
    let found = parent[key];
    if(!found) {
      found = parent[key] = createArray("hashVix");
    }
    return found;
  }

  insert(change:Change) {
    let {getOrCreateHash, getOrCreateArray} = this;
    let eIx = getOrCreateHash(this.eavIndex, change.e);
    let aIx = getOrCreateHash(eIx, change.a);
    let vIx = getOrCreateArray(aIx, change.v);
    vIx.push(change.n, change.transaction, change.round, change.count);

    aIx = getOrCreateHash(this.aveIndex, change.a);
    vIx = getOrCreateHash(aIx, change.v);
    eIx = getOrCreateArray(vIx, change.e);
    eIx.push(change.n, change.transaction, change.round, change.count);

    this.cardinality++;
  }

  resolveProposal(proposal:Proposal) {
    return proposal.info;
  }

  propose(proposal:Proposal, e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number) {
    let forFields = proposal.forFields;
    forFields.length = 0;
    if(isResolved(e)) {
      return this.walkPropose(proposal, this.eavIndex, e, a, v, n, "a", "v", transaction, round);
    } else if(isResolved(a)) {
      return this.walkPropose(proposal, this.aveIndex, a, v, e, n, "v", "e", transaction, round);
    } else {
      // propose for attribute since that's likely to be the smallest
      forFields[0] = "a";
      proposal.info = Object.keys(this.aveIndex);
      proposal.cardinality = proposal.info.length;
    }
    return proposal;
  }

  walkPropose(proposal:Proposal, index:any, a:ResolvedValue, b:ResolvedValue, c:ResolvedValue, n:ResolvedValue,
              fieldB:EAVNField, fieldC:EAVNField, transaction:number, round:number):Proposal {
    let {forFields} = proposal;
    let bIx = index[a as ID];
    if(!bIx) {
      proposal.cardinality = 0;
      return proposal;
    }
    if(isResolved(b)) {
      let cIx = bIx[b];
      if(!cIx) {
        proposal.cardinality = 0;
        return proposal;
      }
      if(isResolved(c)) {
        let ntrcArray = cIx[c];
        if(sumTimes(ntrcArray, transaction, round) > 0) {
          proposal.skip = true;
          return proposal;
        } else {
          proposal.cardinality = 0;
          return proposal;
        }
      } else {
        forFields[0] = fieldC;
        proposal.info = Object.keys(cIx);
        proposal.cardinality = proposal.info.length;
        return proposal;
      }
    } else {
      forFields[0] = fieldB;
      proposal.info = Object.keys(bIx);
      proposal.cardinality = proposal.info.length;
      return proposal;
    }
  }

  // This function checks that there is at least one value in the index that matches the
  // given pattern. If a level is free, we have to run through the potential values
  // until we come across one that could match or we run out of values to check.
  walkCheck(index:any, a:ResolvedValue, b:ResolvedValue, c:ResolvedValue, n:ResolvedValue, transaction:number, round:number):number {
    let bIx = index[a as ID];
    if(!bIx) return 0;
    if(isResolved(b)) {
      let cIx = bIx[b];
      if(!cIx) return 0;
      if(isResolved(c)) {
        let ntrcArray = cIx[c];
        return sumTimes(ntrcArray, transaction, round);
      } else {
        let count = 0;
        for(let key of Object.keys(cIx)) {
          let ntrcArray = cIx[key];
          count += sumTimes(ntrcArray, transaction, round);
        }
        return count;
      }
    } else {
      for(let key of Object.keys(bIx)) {
        let cIx = bIx[key];
        if(!cIx) return 0;
        if(isResolved(c)) {
          let ntrcArray = cIx[c];
          return sumTimes(ntrcArray, transaction, round);
        } else {
          let count = 0;
          for(let key of Object.keys(cIx)) {
            let ntrcArray = cIx[key];
            count += sumTimes(ntrcArray, transaction, round);
          }
          return count;
        }
      }
    }
    return 0;
  }

  check(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):boolean {
    return this.checkMultiplicity(e, a, v, n, transaction, round) > 0;
  }

  checkMultiplicity(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):number {
    if(isResolved(e)) {
      return this.walkCheck(this.eavIndex, e, a, v, n, transaction, round);
    } else if(isResolved(a)) {
      return this.walkCheck(this.aveIndex, a, v, e, n, transaction, round);
    }
    return 0;
  }

  get(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):EAVN[] {
    throw new Error("Not implemented");
  }
}


// @TODO: Implement
class MatrixIndex implements Index {

  constructor() {
    throw new Error("Not implemented");
  }

  insert(change:Change) {
  }

  resolveProposal(proposal:Proposal) {
    return createArray();
  }

  propose(proposal:Proposal, e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number) {
    return proposal;
  }

  check(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):boolean {
    return false;
  }

  checkMultiplicity(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):number {
    throw new Error("@TODO: Implement me!");
  }

  get(e:ResolvedValue, a:ResolvedValue, v:ResolvedValue, n:ResolvedValue, transaction:number, round:number):EAVN[] {
    let final = createArray() as EAVN[];
    return final;
  }
}
