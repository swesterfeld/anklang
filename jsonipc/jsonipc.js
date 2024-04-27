// Licensed CC0 Public Domain: http://creativecommons.org/publicdomain/zero/1.0

/// WebSocket handling coded needed for the Jsonipc wire marshalling
export const Jsonipc = {
  pdefine: globalThis.Object.defineProperty,
  ofreeze: globalThis.Object.freeze,
  okeys: globalThis.Object.keys,
  classes: {},
  receivers: {},
  finalization_registration: () => undefined,
  onbinary: null,
  authresult: undefined,
  web_socket: null,
  counter: null,
  idmap: {},
  cleanup_array_registry: new FinalizationRegistry (callback_array => {
    while (callback_array.length)
      callback_array.pop().call();
  }),
  get_reactive_prop (prop, dflt) {
    if (!this.$props[prop]) {
      if (!this.$props.$unwatchers) {
	this.$props.$unwatchers = [];
	Jsonipc.cleanup_array_registry.register (this, this.$props.$unwatchers, this.$props.$unwatchers);
      }
      this.$props[prop] = new Signal.State (dflt);
      const async_fetch = async () => this.$props[prop].set (await Jsonipc.send ('get/' + prop, [this]));
      async_fetch();
    }
    return this.$props[prop].get();
  },

  /// Open the Jsonipc websocket
  open (url, protocols, options = {}) {
    if (this.web_socket)
      throw "Jsonipc: connection open";
    this.counter = 1000000 * globalThis.Math.floor (100 + 899 * globalThis.Math.random());
    this.idmap = {};
    this.web_socket = new globalThis.WebSocket (url, protocols);
    this.web_socket.binaryType = 'arraybuffer';
    // this.web_socket.onerror = (event) => { throw event; };
    if (options.onclose)
      this.web_socket.onclose = options.onclose;
    this.web_socket.onmessage = this.socket_message.bind (this);
    const promise = new globalThis.Promise ((resolve,reject) => {
      this.web_socket.onopen = (event) => {
	const psend = this.send ('Jsonipc/handshake', []);
	psend.then (result => {
	  this.authresult = result;
	  const protocol = 0x00000001;
	  if (this.authresult == protocol)
	    resolve (true);
	  else
	    reject ("invalid protocoal (" + this.authresult + "), expected: " + protocol);
	});
      };
    });
    return promise;
  },

  Jsonipc_objects: [],

  Jsonipc_prototype: class {
    constructor ($id) {
      Jsonipc.pdefine (this, '$id', { value: $id });
      Jsonipc.pdefine (this, '$props', { value: {} });
      Jsonipc.finalization_registration (this);
    }
    // JSON.stringify replacer
    toJSON() {
      return { $id: this.$id };
    }
    // JSON.parse reviver
    static fromJSON (key, value) {
      if (value?.$id > 0) {
	const JsClass = Jsonipc.classes[value.$class];
	if (JsClass) {
	  let obj = Jsonipc.Jsonipc_objects[value.$id]?.deref();
	  if (!obj) {
	    obj = new JsClass (value.$id);
	    Jsonipc.Jsonipc_objects[value.$id] = new WeakRef (obj);
	  }
	  return obj;
	}
      }
      return value;
    }
  },

  /// Send a Jsonipc request
  async send (method, params) {
    if (!this.web_socket)
      throw "Jsonipc: connection closed";
    const id = ++this.counter;
    this.web_socket.send (globalThis.JSON.stringify ({ id, method, params }));
    const register_reply_handler = resolve => this.idmap[id] = resolve;
    const msg = await new globalThis.Promise (register_reply_handler);
    if (msg.error)
      throw globalThis.Error (
	`${msg.error.code}: ${msg.error.message}\n` +
	`Request: {"id":${id},"method":"${method}",…}\n` +
	"Reply: " + globalThis.JSON.stringify (msg)
      );
    return msg.result;
  },

  /// Observe Jsonipc notifications
  receive (methodname, handler) {
    if (handler)
      this.receivers[methodname] = handler;
    else
      delete this.receivers[methodname];
  },

  /// Handle binary messages
  handle_binary (handler) {
    this.onbinary = handler ? handler : null;
  },

  /// Handle a Jsonipc message
  socket_message (event) {
    // Binary message
    if (event.data instanceof globalThis.ArrayBuffer)
      {
	const handler = this.onbinary;
	if (handler)
	  handler (event.data);
	else
	  globalThis.console.error ("Unhandled message event:", event);
	return;
      }
    // Text message
    const maybe_prototype = event.data.indexOf ('"$class":"') >= 0;
    const msg = globalThis.JSON.parse (event.data, maybe_prototype ? Jsonipc.Jsonipc_prototype.fromJSON : null);
    if (msg.id)
      {
	const handler = this.idmap[msg.id];
	delete this.idmap[msg.id];
	if (handler)
	  return handler (msg);
      }
    else if ("string" === typeof msg.method && globalThis.Array.isArray (msg.params)) // notification
      {
	const receiver = this.receivers[msg.method];
	if (receiver)
	  receiver.apply (null, msg.params);
	return;
      }
    globalThis.console.error ("Unhandled message:", event.data);
  },

  /// Simplify initialization of globals
  setup_promise_type (type, resolved = undefined) {
    let resolve;
    const p = new Promise (r => resolve = r);
    p.__resolve__ = instance => {
      if (instance instanceof type) {
	resolve (instance);
	if (resolved)
	  resolved (instance);
      }
    };
    return p;
  },
};

// ----- End of jsonipc/jsonipc.js -----
