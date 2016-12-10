//---------------------------------------------------------------------
// Browser
//---------------------------------------------------------------------

import {Evaluation, Database} from "../runtime/runtime";
import * as join from "../runtime/join";
import {EveClient, client} from "./client";
import * as parser from "../runtime/parser";
import * as builder from "../runtime/builder";
import {ids} from "../runtime/id";
import {RuntimeClient} from "../runtime/runtimeClient";
import {HttpDatabase} from "../runtime/databases/http";
import {BrowserViewDatabase, BrowserEditorDatabase, BrowserInspectorDatabase, BrowserServerDatabase} from "../runtime/databases/browserSession";

//---------------------------------------------------------------------
// Utils
//---------------------------------------------------------------------

// this makes me immensely sad...
function download(filename, text) {
  var element = document.createElement('a');
  element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));
  element.setAttribute('download', filename);
  element.style.display = 'none';
  document.body.appendChild(element);
  element.click();
  document.body.removeChild(element);
}

//---------------------------------------------------------------------
// Responder
//---------------------------------------------------------------------

class BrowserRuntimeClient extends RuntimeClient {
  client: EveClient;

  constructor(client:EveClient) {
    let dbs = {
      "http": new HttpDatabase(),
      "server": new BrowserServerDatabase(client)
    }
    if(client.showIDE) {
      dbs["view"] = new BrowserViewDatabase();
      dbs["editor"] = new BrowserEditorDatabase();
      dbs["inspector"] = new BrowserInspectorDatabase();
    }
    super(dbs);
    this.client = client;
  }

  send(json) {
    setTimeout(() => {
      this.client.onMessage({data: json});
    }, 0);
  }

}

export var responder: BrowserRuntimeClient;

//---------------------------------------------------------------------
// Init a program
//---------------------------------------------------------------------

export function init(code) {
  global["browser"] = true;

  responder = new BrowserRuntimeClient(client);
  responder.load(code || "", "user");

  global["evaluation"] = responder;

  global["save"] = () => {
    responder.handleEvent(JSON.stringify({type: "dumpState"}));
  }

  // client.socket.onopen();
  // responder.handleEvent(JSON.stringify({type: "findPerformance", requestId: 2}));
}
