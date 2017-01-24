import {ChangeSet, createChangeSet} from "./runtime/runtime";

/**
 * Convenient shorthand for indicating a concrete subclass of an
 * abstract class.
 */
interface SubClass<Class> {
  new(...args: any[]): Class
};

//------------------------------------------------------------------------
// Connection
//------------------------------------------------------------------------

/**
 * The connection is the lowest level of the Eve network stack.  It's
 * a simple transport layer abstraction that connects the current Eve
 * instance to another Eve instance.It has built in support for
 * queueing and should generally be kept as non-specific as
 * possible. All messages sent over a connection are ChangeSets and
 * all messages received must be converted back into ChangeSets.
 */

abstract class Connection {
  protected _host:string;
  protected _connected = false;
  protected _queue:ChangeSet[] = [];

  get host() { return this._host; }
  get connected() { return this._connected; }
  get queued() { return this._queue.length; }

  send(changes:ChangeSet):boolean {
    if(this.connected) {
      this.rawSend(changes);
      return true;
    }

    this._queue.push(changes);
    return false;
  }

  protected _onData = (changes:ChangeSet) => {
    if(this.onData) {
      this.onData(changes);
    }
  }

  protected _onOpen = (host:string) => {
    this._connected = true;
    this._host = host;
    if(this.queued) {
      for(let changes of this._queue) {
        this.send(changes);
      }
    }
    if(this.onOpen) {
      this.onOpen();
    }
  }

  protected _onClose = (reason:string = "Connection terminated by peer.") => {
    this._connected = false;
    if(this.onClose) {
      this.onClose(reason);
    }
  }

  protected _onError = (error:Error) => {
    this._connected = false;
    if(this.onError) {
      this.onError(error);
    }
  }

  // These lifecycle callbacks may be provided by the connection's parent.
  // If they exist they'll be invoked by the lifecycle methods (E.g.:
  // `_onOpen` will invoke `onOpen`.
  onData?: (changes:ChangeSet) => void;
  onOpen?: () => void;
  onClose?: (reason:string) => void;
  onError?: (error:Error) => void;


  // These methods must be implemented by the subclass.
  // In addition, lifecycle methods (_onData, _onOpen, _onClose, and _onError)
  // *MUST* be appropriately triggered.
  abstract open(host:string):void;
  abstract close():void;
  abstract rawSend(changes:ChangeSet):void;
}

class TestConnection extends Connection {
  protected static _id = 0;

  /** connections is a map from a destination host to the source connection. */
  static origins:{[origin:string]: TestConnection} = {};
  /**
   * endpoints is a map from a host to its connection. These are
   * added using `TestConnection.addEndPoint(host, connection)`.
   */
  static endpoints:{[host:string]: TestConnection} = {};

  static addEndPoint(origin:string, conn:TestConnection) {
    conn._origin = origin;
    TestConnection.origins[origin] = conn;
    let other = conn._endpoint = TestConnection.endpoints[origin];
    if(other) {
      other._endpoint = conn;
      other._onOpen(other.host);
      conn._onOpen(other._origin);
    }
  }

  protected _origin = `testhost:${TestConnection._id++}`;
  protected _endpoint:TestConnection;

  open(host:string) {
    if(TestConnection.endpoints[host]) {
      this._onError(new Error("Unable to connect; destination already in use."));

    } else if (TestConnection.origins[this._origin]) {
      this._onError(new Error("Unable to connect; origin already in use."));

    } else {
      TestConnection.endpoints[host] = this;
      TestConnection.origins[this._origin] = this;
      let other = this._endpoint = TestConnection.origins[host];
      if(other) {
        other._endpoint = this;
        other._onOpen(this._origin);
        this._onOpen(host);
      }
    }
  }

  close() {
    TestConnection.origins[this._origin] = undefined as any;
    TestConnection.endpoints[this.host] = undefined as any;
    this._onClose();
    TestConnection.origins[this.host]._onClose();
  }

  rawSend(changes:ChangeSet) {
    setTimeout(() => {
      console.log(`Sending ${this._origin} -> ${this.host}`);
      let other = TestConnection.origins[this.host];
      if(other) {
        other._onData(changes);
      } else if(this.connected) {
        this._onError(new Error("Endpoint hung up."));
      }
    }, Math.floor(Math.random() * 100));
  }
}

//------------------------------------------------------------------------
// Peer
//------------------------------------------------------------------------

/**
 * Peers allow the Network's creator to keep state about the program
 * on the other side of the connection using the data KV store
 * via the get and set functions. This should allow an easy swap to a
 * facts-based store assuming we decide to hoist this into Eve.
 */
abstract class Peer {
  protected _data:{[key:string]: any|undefined} = {};

  get connection() { return this._connection; }

  constructor(protected _connection:Connection) {}

  connect(host:string) {
    this._connection.open(host);
  }

  disconnect() {
    this._connection.close();
  }

  send(changes:ChangeSet) {
    this._connection.send(changes);
  }

  get(key:string):any|undefined {
    return this._data[key];
  }
  set(key:string, value:any) {
    this._data[key] = value;
  }

  protected _onDisconnect = () => {
    if(this.onDisconnect) {
      this.onDisconnect();
    }
  }

  onDisconnect?: () => void;
}

class TestPeer extends Peer {
  constructor(connection:Connection) {
    super(connection);
    connection.onData = (changes:ChangeSet) => {
      console.log(changes.join("\n"));
    };
  }
}

//------------------------------------------------------------------------
// Watcher
//------------------------------------------------------------------------

class Watcher {
}

//------------------------------------------------------------------------
// Network
//------------------------------------------------------------------------

/**
 * A network manages connections with multiple peers.
 */
class Network {
  /** Maps hosts to peers 1:1 */
  protected _peers:{[host:string]: Peer} = {};

  constructor() {}

  // Connect to a peer at `host` as a `PeerClass` (e.g. ClientPeer)
  // using a `ConnectionClass` (e.g. WebWorkerConnection).
  connect(host:string, PeerClass:SubClass<Peer>, ConnectionClass:SubClass<Connection>):Peer {
    if(this._peers[host]) {
      return this._peers[host];

    } else {
      let peer = new PeerClass(new ConnectionClass());
      this._peers[host] = peer;
      peer.onDisconnect = () => this._peerDisconnected(peer);
      peer.connect(host);
      return peer;
    }
  }

  disconnect(peer:Peer) {
    peer.disconnect();
  }

  send(peer:Peer, changes:ChangeSet) {
    peer.send(changes);
  }

  protected _peerDisconnected = (peer:Peer) => {
    if(this.onPeerDisconnected) {
      this.onPeerDisconnected(peer);
    }
    this._peers[peer.connection.host] = undefined as any;
  }

  onPeerDisconnected?:(peer:Peer) => void;
}

//------------------------------------------------------------------------------
// Testing logic
//------------------------------------------------------------------------------

function doIt() {
  let test123 = new TestConnection();
  test123.onData = (changes:ChangeSet) => {
    console.log(changes.join("\n"));
  }
  TestConnection.addEndPoint("test:123", test123);

  let network = new Network();
  let myFriend = network.connect("test:123", TestPeer, TestConnection);

  network.send(myFriend, createChangeSet(["<1>", "tag", "pet", 1]));
  test123.send(createChangeSet(["<2>", "name", "Philbert", 1]));
}

// If we're running in Node and were run directly.
if(typeof require !== "undefined" && require.main === module) {
  for(let ix = 0; ix < 1; ix++) {
    doIt();
  }
}
