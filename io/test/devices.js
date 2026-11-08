// Prints what thunder_io.node reports. Run after `node io/build.js`:  node io/test/devices.js
'use strict';
const path = require('path');
const I = require(path.join(__dirname, '..', '..', 'electron', 'io', 'thunder_io.node'));
console.log('version:', I.version());
try { console.log('devices():', JSON.stringify(I.devices(), null, 2)); } catch (e) { console.log('devices() threw:', e.message); }
try { console.log("devices({backend:'null'}):", JSON.stringify(I.devices({ backend: 'null' }), null, 2)); } catch (e) { console.log("devices({backend:'null'}) threw:", e.message); }
const a = I.now(), b = I.now();
console.log('now():', a, b, 'delta ns:', b - a);
