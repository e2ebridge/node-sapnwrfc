const path = require('path');

let buildType = (process.env['NODE_SAPNWRFC_BUILD_TYPE'] || 'Release');
exports.buildType = buildType.charAt(0).toUpperCase() + buildType.slice(1);
exports.buildPath = path.resolve(__dirname, `./build/${process.platform}/${process.arch}`);
exports.modulePath = path.join(exports.buildPath, `${buildType}/sapnwrfc.node`);