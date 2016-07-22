// A message for use with the Socket class
function Message(type, data, id = 0, timestamp = 0)
{
	this.id = id;
	this.type = type;
	this.data = data;
	this.timestamp = timestamp;

	function isEqual(messageB) {
		if (this == messageB) {
			return true;
		}
		else if (this.id == messageB.id && this.timestamp == messageB.timestamp && isDateTypeEqual(messageB.type)) {
			return isDataEqual(messageB);
		}
		return false;
	}

	function isDateTypeEqual(typeB)  {
		return this.type == typeB;
	}

	function isDataEqual(dataB) {
		if (this.data == dataB) {
			return true;
		}
		else if (typeof(this.data) == typeof(dataB) && this.data.length == dataB.length) {
			const length = this.data.length;
			for (var i = 0; i < length; ++i) {
				if (this.data[i] != dataB[i]) {
					return false;
				}
			}
			return true;
		}
		return false;
	}

	function isMessageSent() {
		return this.id != 0 && this.timestamp != 0;
	}
}

// Class handling websocket communication
// Create using e.g. socket = new Socket(location.hostname);
// Then call 
// You can pass a function to be called when the server sends a message
// in onServerMessage. The signature should be function(messageData).
function Socket(hostName)
{
	this.hostname = hostname; // the server host name to connect to
	this.serverMessageCallback; // function called when a server message is received
	this.lastMessageId = 0; // message identifier. this will increase for every message
	this.messageQueue = []; // list of messages currently sent (#0) or queued (#1+) with their id, type and binary data
	this.lastMessageSent; // the last sucessfully sent message
	this.connection; // the actual websocket connection

	function initialize() {
		this.connection = new WebSocket('ws://' + this.hostname + ':81/');
		this.connection.binaryType = "arraybuffer";
		this.connection.onopen = function () { console.log('WebSocket opened'); };
		this.connection.onclose = function () { console.log('WebSocket closed'); };
		this.connection.onerror = function (e) { console.log('WebSocket error: ', e); };
		this.connection.onmessage = function (e) { 
			console.log('Server message: ', e.data);
			this.processServerResponse(e.data);
		};
	}

	function onServerMessage(callback) {
		this.serverMessageCallback = callback;
	}

	function isLastQueueEntryOfDataType(dataType) {
		return this.messageQueue.length > 0 && this.messageQueue[this.messageQueue.length - 1][1] == dataType;
	}

	function areSentMessageInQueue() {
		for (var i = 0; i < this.messageQueue.length; ++i) {
			if (this.isMessageAlreadySent(this.messageQueue[i])) {
				return true;
			}
		}
		return false;
	}

	function processServerResponse(responseData) {
		// create view on response data
		var response = new DataView(responseData);
		// check if the response data is long enough
		if (response.byteLength >= Uint32Array.BYTES_PER_ELEMENT) {
			// retrieve the message id and convert to big-endian
			var messageId = response.getUint32(0, true);
			// check if this is a server command or response
			if (messageId == 0) {
				// server command
				serverMessageCallback(responseData);
			}
			else {
				// response. find message in queue
				if (response.byteLength >= Uint32Array.BYTES_PER_ELEMENT + 1) {
					// get response success
					var success = response.getUint8(Uint32Array.BYTES_PER_ELEMENT);
					console.log('Last command result: ' + success);
					for (var i = 0; i < messageQueue.length; ++i) {
						// if found message with same id, remove queue entry
						if (messageQueue[i][0] == messageId) {
							// calculate turn-around time
							var turnAroundTimeMs = Date.now() - messageQueue[i][3];
							console.log('Turn-around time: ' + turnAroundTimeMs + 'ms');
							// store message as last one and remove from queue
							lastMessageSent = messageQueue[i];
							messageQueue.splice(i, 1);
							break;
						}
					}
				}
				// if there is a message in the queue, send it now if it hasn't been sent yet
				if (messageQueue.length > 0 && !isMessageAlreadySent(messageQueue[0])) {
					messageQueueEntry(messageQueue[0]);
				}
			}
		}
	}

	function messageQueueEntry(queueEntry)
	{
		// check if the message has not already been sent
		if (isMessageAlreadySent(queueEntry)) {
			console.log('Message #' + queueEntry[0] + ' already sent! Will not send again.');
		}
		else {
			// increase message id and write to queue
			lastMessageId++;
			queueEntry[0] = lastMessageId;
			console.log('Sending message #' + lastMessageId);
			// write timestamp to queue
			queueEntry[3] = Date.now();
			// combine message id and data
			var combinedData = new ArrayBuffer(Uint32Array.BYTES_PER_ELEMENT + queueEntry[2].byteLength); // id + data
			var uint8View = new Uint8Array(combinedData); // uint8 view onto combinedData
			uint8View.set(queueEntry[2], Uint32Array.BYTES_PER_ELEMENT);
			// build view onto combined data and set identifier in little-endian format
			var combinedView = new DataView(combinedData);
			combinedView.setUint32(0, lastMessageId, true);
			// finally send message to server
			connection.send(combinedData);
		}
	}

	function sendWebSocketMessage(data, dataType, overwrite = false) {
		// check if we've just sent this message before
		if (lastMessageSent && lastMessageSent[1] == dataType && isQueueEntryDataEqual(lastMessageSent[2], data)) {
			console.log('Message already sent! Will not send again.');
			return;
		}
		// check if there's a duplicate message in the queue already
		if (isLastQueueEntryOfDataType(dataType)) {
			// check if message has already been sent
			if (isMessageAlreadySent(messageQueue[messageQueue.length - 1])) {
				// message sent already. check if data is the same
				if (isQueueEntryDataEqual(messageQueue[messageQueue.length - 1][2], data)) {
					// data type and data is the same, so do not send again
					console.log('Duplicate message! Will not send again.');
					return;
				}
			}
			else if (overwrite){
				// message not already sent and overwrite specified
				// the message is of the same type so we can just safely replace its data
				messageQueue[messageQueue.length - 1][2] = data;
				console.log('Message data replaced!');
				return;
			}
		}
		// create new queue entry, and mark its timestamp as "not sent yet" == 0 and push it to queue
		var queueEntry = [0, dataType, data, 0];
		messageQueue.push(queueEntry);
		// check if there are messages pending to be sent
		if (!areSentMessageInQueue()) {
			// no sent messages in queue, so send instantly
			messageQueueEntry(queueEntry);
		}
	}
}
