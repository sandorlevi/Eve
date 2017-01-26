import {EAVN, ID} from "../../runtime/runtime";

interface Diff { add: Readonly<EAVN>[], remove: Readonly<EAVN>[] }

interface EAVNC extends EAVN {
  count: number
}

type WatchResults = [void] | any[]; // This tells the type system that we're expecting something tuple-ish rather than union-y.

interface DSLFind {}
interface DSLLib {}
interface DSLSubscribe { (entity:any): Diff }
interface DSLObject { <Attrs>(entity:any, attributes:Attrs): Readonly<Attrs>[] }

interface DSLSearch<T extends WatchResults> { (find:DSLFind, lib:DSLLib, subscribe:DSLSubscribe, object:DSLObject):T }
interface DSLWatchHandler<T extends WatchResults> { (args:T):void }

function watch<T extends WatchResults>(search:DSLSearch<T>, handler:DSLWatchHandler<T>) {

}

watch((find, lib, subscribe, object) => [
  subscribe("foo"),
  object("bar", {foo: 5, baz: 3})
], ([eavns, objs]) => {
});


interface Style {
  [attribute:string]: ID
}

class HTMLRenderer {
  protected _elements:HTMLElement[] = [];
  protected _styles:{[id:number]: Style} = {};


}
