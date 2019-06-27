const red = '\u001b[31m',
    green = '\u001b[32m',
    reset = '\u001b[0m';

const load = require('./load');

try {
    require(load.modulePath);
    console.log(`${green}ok ${reset}found precompiled '${load.buildType}' module`);
} catch(e) {
    console.log(e);
    console.log(`${red}error ${reset}a precompiled '${load.buildType}' module could not be found or loaded`);
    console.log(`${green}info ${reset}trying to compile it...`);
    process.argv = [process.argv[0], process.argv[1], 'rebuild', '-O', load.buildPath];
    if(load.buildType === 'Debug') {
        process.argv.push('--debug');
    }
    require('cmake-js/bin/cmake-js');
}
