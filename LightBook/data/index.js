window.addEventListener('load', initialize, false);
window.addEventListener('DOMContentLoaded', initialize, false);
window.addEventListener('resize', windowResized, false);

var DISPLAY_WIDTH = 4;
var DISPLAY_HEIGHT = 4;
var backBuffer = new Uint8Array(DISPLAY_WIDTH * DISPLAY_HEIGHT * 3);

var display;
var canvas;
var context;
var clearButton;
var isMouseDownOnCanvas = false;
var isMouseDownOnLed = false;
var isInitialized = false;

var frontColorButton;
var backColorButton;
var frontColor = '#000000';
var backColor = '#000000';
var frontColorSelected = true;

const TOOL_PEN = 'pen';
const TOOL_PICKER = 'picker';
var toolSelected = TOOL_PEN;
var penToolButton;
var pickToolButton;

const MESSAGE_DISPLAY_SIZE = 's'; // identifies a message from the server that sends the size of the display
const MESSAGE_LED_COLOR = 'l'; // identifies a single led color message
const MESSAGE_LED_FRAME = 'f'; // identifies a full-frame color message
var webSocketMessageId = 0; // message identifier. this will increase for every message
var webSocketSendQueue = []; // list of messages currently sent (#0) or queued (#1+) with their id, type and binary data
var webSocketLastMessageSent; // the last sucessfully sent message
var webSocketConnection;

function initializeWebSocket() {
	webSocketConnection = new WebSocket('ws://' + location.hostname + ':81/');
	webSocketConnection.binaryType = "arraybuffer";
	webSocketConnection.onopen = function () { console.log('WebSocket opened'); };
	webSocketConnection.onclose = function () { console.log('WebSocket closed'); };
	webSocketConnection.onerror = function (e) { console.log('WebSocket error: ', e); };
	webSocketConnection.onmessage = function (e) { 
		console.log('Server message: ', e.data);
		webSocketProcessResponse(e.data);
	};
}

function isQueueEntryDataEqual(dataA, dataB) {
	if (dataA == dataB) {
		return true;
	}
	else if (dataA.length == dataB.length) {
		const length = dataA.length;
		for (var i = 0; i < length; ++i) {
			if (dataA[i] != dataB[i]) {
				return false;
			}
		}
		return true;
	}
	return false;
}

function isLastQueueEntryOfDataType(dataType) {
	return webSocketSendQueue.length > 0 && webSocketSendQueue[webSocketSendQueue.length - 1][1] == dataType;
}

function isMessageAlreadySent(queueEntry) {
	return queueEntry[0] != 0 || queueEntry[3] != 0;
}

function areSentMessageInQueue() {
	for (var i = 0; i < webSocketSendQueue.length; ++i) {
		if (isMessageAlreadySent(webSocketSendQueue[i])) {
			return true;
		}
	}
	return false;
}

function webSocketProcessResponse(responseData) {
	// create view on response data
	var response = new DataView(responseData);
	// check if the response data is long enough
	if (response.byteLength >= Uint32Array.BYTES_PER_ELEMENT) {
		// retrieve the message id and convert to big-endian
		var messageId = response.getUint32(0, true);
		// check if this is a server command or response
		if (messageId == 0) {
			// server command. check what it is
            if (response.getUint8(Uint32Array.BYTES_PER_ELEMENT) == MESSAGE_DISPLAY_SIZE.charCodeAt()) {
                // new LED display size, dissect message
                var newWidth = response.getUint8(Uint32Array.BYTES_PER_ELEMENT + 1);
                var newHeight = response.getUint8(Uint32Array.BYTES_PER_ELEMENT + 2);
                console.log('New display size: ' + newWidth + "x" + newHeight);
                setDisplaySize(newWidth, newHeight);
            }
		}
		else {
			// server response to our message. find message in queue
			if (response.byteLength >= Uint32Array.BYTES_PER_ELEMENT + 1) {
				// get response success
				var success = response.getUint8(Uint32Array.BYTES_PER_ELEMENT);
				console.log('Last command result: ' + success);
				for (var i = 0; i < webSocketSendQueue.length; ++i) {
					// if found message with same id, remove queue entry
					if (webSocketSendQueue[i][0] == messageId) {
						// calculate turn-around time
						var turnAroundTimeMs = Date.now() - webSocketSendQueue[i][3];
						console.log('Turn-around time: ' + turnAroundTimeMs + 'ms');
						// store message as last one and remove from queue
						webSocketLastMessageSent = webSocketSendQueue[i];
						webSocketSendQueue.splice(i, 1);
						break;
					}
				}
			}
			// if there is a message in the queue, send it now if it hasn't been sent yet
			if (webSocketSendQueue.length > 0 && !isMessageAlreadySent(webSocketSendQueue[0])) {
				webSocketSendQueueEntry(webSocketSendQueue[0]);
			}
		}
	}
}

function webSocketSendQueueEntry(queueEntry)
{
	// check if the message has not already been sent
	if (isMessageAlreadySent(queueEntry)) {
		console.log('Message #' + queueEntry[0] + ' already sent! Will not send again.');
	}
	else {
		// increase message id and write to queue
		webSocketMessageId++;
		queueEntry[0] = webSocketMessageId;
		console.log('Sending message #' + webSocketMessageId);
		// write timestamp to queue
		queueEntry[3] = Date.now();
		// combine message id and data
		var combinedData = new ArrayBuffer(Uint32Array.BYTES_PER_ELEMENT + queueEntry[2].byteLength); // id + data
		var uint8View = new Uint8Array(combinedData); // uint8 view onto combinedData
		uint8View.set(queueEntry[2], Uint32Array.BYTES_PER_ELEMENT);
		// build view onto combined data and set identifier in little-endian format
		var combinedView = new DataView(combinedData);
		combinedView.setUint32(0, webSocketMessageId, true);
		// finally send message to server
		webSocketConnection.send(combinedData);
	}
}

function sendWebSocketMessage(data, dataType, overwrite = false) {
	// check if we've just sent this message before
	if (webSocketLastMessageSent && webSocketLastMessageSent[1] == dataType && isQueueEntryDataEqual(webSocketLastMessageSent[2], data)) {
		console.log('Message already sent! Will not send again.');
		return;
	}
	// check if there's a duplicate message in the queue already
	if (isLastQueueEntryOfDataType(dataType)) {
		// check if message has already been sent
		if (isMessageAlreadySent(webSocketSendQueue[webSocketSendQueue.length - 1])) {
			// message sent already. check if data is the same
			if (isQueueEntryDataEqual(webSocketSendQueue[webSocketSendQueue.length - 1][2], data)) {
				// data type and data is the same, so do not send again
				console.log('Duplicate message! Will not send again.');
				return;
			}
		}
		else if (overwrite){
			// message not already sent and overwrite specified
			// the message is of the same type so we can just safely replace its data
			webSocketSendQueue[webSocketSendQueue.length - 1][2] = data;
			console.log('Message data replaced!');
			return;
		}
	}
	// create new queue entry, and mark its timestamp as "not sent yet" == 0 and push it to queue
	var queueEntry = [0, dataType, data, 0];
	webSocketSendQueue.push(queueEntry);
	// check if there are messages pending to be sent
	if (!areSentMessageInQueue()) {
		// no sent messages in queue, so send instantly
		webSocketSendQueueEntry(queueEntry);
	}
}

function sendLedColor(lX, lY) {
	var index = 3 * (lY * DISPLAY_WIDTH + lX);
	var data = new Uint8Array([MESSAGE_LED_COLOR.charCodeAt(), lX, lY, backBuffer[index], backBuffer[index + 1], backBuffer[index + 2]]); //'l' + x + y + color
	sendWebSocketMessage(data, MESSAGE_LED_COLOR);
}

function sendLedFrame() {
	var data = new Uint8Array(3 + backBuffer.byteLength); //'f' + w + h + frame data
	data.set(new Uint8Array([MESSAGE_LED_FRAME.charCodeAt(), DISPLAY_WIDTH, DISPLAY_HEIGHT]), 0);
	data.set(backBuffer, 3);
	sendWebSocketMessage(data, MESSAGE_LED_FRAME, true);
}

function colorHexToBin(c) {
	if (c.substr(0, 1) === '#') {
		var a = [];
		for (var i = 1, len = c.length; i < len; i+=2) {
			a.push(parseInt(c.substr(i,2),16));
		}
		return new Uint8Array(a);
	}
	else if (c.substr(0,3) === 'rgb') {
		var nums = /(.*?)rgb\((\d+),\s*(\d+),\s*(\d+)\)/i.exec(c);
		return new UintArray([parseInt(nums[2], 10), parseInt(nums[3], 10), parseInt(nums[4], 10)]);
	}
	return new Uint8Array();
}

function colorRgbToHex(r, g, b) {
	if (typeof r === 'string' || r instanceof String) {
		if (r.substr(0, 1) === '#') {
			return r;
		}
		else if (r.substr(0,3) === 'rgb') {
			var nums = /(.*?)rgb\((\d+),\s*(\d+),\s*(\d+)\)/i.exec(r),
				cr = parseInt(nums[2], 10).toString(16),
				cg = parseInt(nums[3], 10).toString(16),
				cb = parseInt(nums[4], 10).toString(16);
			return '#' + ((cr.length == 1 ? '0' + cr : cr) + (cg.length == 1 ? '0' + cg : cg) + (cb.length == 1 ? '0' + cb : cb));
		}
	}
	else {
		return '#' + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
	}
	return;
}

function setAllLedColors(color) {
	var hexColor = colorRgbToHex(color);
	var binColor = colorHexToBin(color);
	for (var r = 0; r < display.rows.length; r++) {
		var row = display.rows[r];
		for (var c = 0; c < row.cells.length; c++) {
			var cell = row.cells[c];
			cell.style.backgroundColor = hexColor;
			var index = 3 * (r * DISPLAY_WIDTH + c);
			backBuffer[index] = binColor[0];
			backBuffer[index + 1] = binColor[1];
			backBuffer[index + 2] = binColor[2];
		}
	}
	sendLedFrame();
}

function ledPicked(lX, lY) {
	if (toolSelected == TOOL_PEN) {
		var color = frontColorSelected ? frontColor : backColor;
		var hexColor = colorRgbToHex(color);
		var cell = display.rows[lY].cells[lX];
		if (cell.style.backgroundColor != hexColor) {
			cell.style.backgroundColor = hexColor;
			var index = 3 * (lY * DISPLAY_WIDTH + lX);
			var binColor = colorHexToBin(color);
			backBuffer[index] = binColor[0];
			backBuffer[index + 1] = binColor[1];
			backBuffer[index + 2] = binColor[2];
			sendLedColor(lX, lY);
		}
	}
	else if (toolSelected == TOOL_PICKER) {
		var pickedColor = display.rows[lY].cells[lX].style.backgroundColor;
		var hexColor = colorRgbToHex(pickedColor);
		if (frontColorSelected) {
			frontColor = hexColor;
			frontColorButton.style.backgroundColor = hexColor;
		}
		else {
			backColor = hexColor;
			backColorButton.style.backgroundColor = hexColor;
		}
	}
}

function setFrontBackColor(pX, pY) {
	var color = context.getImageData(pX, pY, 1, 1).data;
	var hexColor = colorRgbToHex(color[0], color[1], color[2]);
	if (frontColorSelected) {
		frontColor = hexColor;
		frontColorButton.style.backgroundColor = hexColor;
	}
	else {
		backColor = hexColor;
		backColorButton.style.backgroundColor = hexColor;
	}
}

function setDisplaySize(newWidth, newHeight) {
	if (newWidth != DISPLAY_WIDTH || newHeight != DISPLAY_HEIGHT) {
		DISPLAY_WIDTH = newWidth;
		DISPLAY_HEIGHT = newHeight;
		backBuffer = new Uint8Array(DISPLAY_WIDTH * DISPLAY_HEIGHT * 3);
		buildDisplayTable(newWidth, newHeight);
	}
}

function buildDisplayTable(width, height) {
	// clear table first
	while (display.rows.length > 0) {
		display.deleteRow(0);
	}
	// now insert new row and columns
	for (var r = 0; r < height; r++) {
		var tr = display.insertRow();
		for (var c = 0; c < width; c++) {
			var cell = tr.insertCell();
			cell.className = 'led';
			// add event listener when user clicks on a led or moves over it with mouse down
			cell.addEventListener('mousedown', 
			function (x, y, evt) {
				isMouseDownOnLed = true;
				ledPicked(x, y);
			}.bind(cell, c, r, undefined));
			cell.addEventListener('mouseup', 
			function (x, y, evt) {
				isMouseDownOnLed = false;
			}.bind(cell, c, r, undefined));
			cell.addEventListener('mousemove', 
			function (x, y, evt) {
				if (isMouseDownOnLed) {
					ledPicked(x, y);
				}
			}.bind(cell, c, r, undefined));
			cell.addEventListener('touchmove', 
			function (x, y, evt) {
				ledPicked(x, y);
				evt.preventDefault();
			}.bind(cell, c, r, undefined));
		}
	}
}

function addEventListeners()
{
	// add event listeners to the canvas for mouse move events
	canvas.addEventListener('mousedown',
	function (evt) {
		isMouseDownOnCanvas = true;
		var x = evt.offsetX;//evt.pageX - canvas.offsetLeft;
		var y = evt.offsetY;//evt.pageY - canvas.offsetTop;
		setFrontBackColor(x, y);
	});
	window.addEventListener('mouseup', 
	function (evt) {
		isMouseDownOnCanvas = false;
	});
	canvas.addEventListener('mousemove',
	function (evt) {
		if (isMouseDownOnCanvas) {
			var x = evt.offsetX;//evt.pageX - canvas.offsetLeft;
			var y = evt.offsetY;//evt.pageY - canvas.offsetTop;
			setFrontBackColor(x, y);
		}
	});
	canvas.addEventListener('touchmove', 
	function(evt) {
		var x = evt.offsetX;//evt.pageX - canvas.offsetLeft;
		var y = evt.offsetY;//evt.pageY - canvas.offsetTop;
		setFrontBackColor(x, y);
		evt.preventDefault();
	});
	document.ontouchmove = function(e) { e.preventDefault(); };
	// add event listener to display clear button
	clearButton.addEventListener('click', 
	function(evt) {
		var color = frontColorSelected ? frontColor : backColor;
		setAllLedColors(color);
	});
	// add event listeners to front and back color buttons
	frontColorButton.addEventListener('click', 
	function(evt) {
		frontColorButton.className = 'colorTdActive';
		backColorButton.className = 'colorTd';
		frontColorSelected = true;
	});
	backColorButton.addEventListener('click', 
	function(evt) {
		frontColorButton.className = 'colorTd';
		backColorButton.className = 'colorTdActive';
		frontColorSelected = false;
	});
	// add event listeners to tool buttons
	penToolButton.addEventListener('click', 
	function(evt) {
		toolSelected = TOOL_PEN;
		penToolButton.className = 'iconTdActive';
		pickToolButton.className = 'iconTd';
	});
	pickToolButton.addEventListener('click', 
	function(evt) {
		toolSelected = TOOL_PICKER;
		penToolButton.className = 'iconTd';
		pickToolButton.className = 'iconTdActive';
	});
}

function windowResized() {
	fillColorPicker();
}

function fillColorPicker() {
	var colors = context.createLinearGradient(0, 0, context.canvas.width, 0);
	for(var i = 0; i <= 360; i += 10) {
		colors.addColorStop(i / 360, 'hsl(' + i + ', 100%, 50%)');
	}
	context.fillStyle = colors;
	context.fillRect(0, 0,context.canvas.width, context.canvas.height);
	var luminance = context.createLinearGradient(0, 0, 0, context.canvas.height);
	luminance.addColorStop(0, '#ffffff');
	luminance.addColorStop(0.05, '#ffffff');
	luminance.addColorStop(0.5, 'rgba(0,0,0,0)');
	luminance.addColorStop(0.95, '#000000');
	luminance.addColorStop(1, '#000000');
	context.fillStyle = luminance;
	context.fillRect(0, 0, context.canvas.width, context.canvas.height);
}

function initialize() {
	if (!isInitialized) {
		isInitialized = true;
		canvas = document.getElementById('colorpicker');
		context = canvas.getContext('2d');
		display = document.getElementById('display');
		frontColorButton = document.getElementById('frontColor');
		backColorButton = document.getElementById('backColor');
		clearButton = document.getElementById('clearButton');
		penToolButton = document.getElementById('penToolButton');
		pickToolButton = document.getElementById('pickToolButton');
		fillColorPicker();
		buildDisplayTable(DISPLAY_WIDTH, DISPLAY_HEIGHT);
		addEventListeners();
		initializeWebSocket();
	}
}
