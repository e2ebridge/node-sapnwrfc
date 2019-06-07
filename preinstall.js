var red = '\u001b[31m',
  green = '\u001b[32m',
  reset = '\u001b[0m';

try {
  var majMinVersion = process.versions.node.match(/^[0-9]+.[0-9]+/)[0] || '';
  var bindings = require('bindings')({ bindings: 'sapnwrfc', version: majMinVersion });
  console.log(green + 'ok ' + reset + 'found precompiled module at ' + bindings.path);
} catch (e) {
  console.log(e);
  console.log(red + 'error ' + reset + 'a precompiled module could not be found or loaded');
  console.log(green + 'info ' + reset + 'trying to compile it...');
  require('cmake-js/bin/cmake-js');
}
