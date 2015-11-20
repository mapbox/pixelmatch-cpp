'use strict';

var fs = require('fs');
var path = require('path');
var PNG = require('pngjs').PNG;

var base = 'test/fixtures';

fs.readdirSync(base).filter(function(name) {
    return path.extname(name) === '.png';
}).forEach(function(name) {
    var img = fs.createReadStream(path.join(base, name))
      .pipe(new PNG())
      .on('parsed', function () {
          fs.writeFileSync('test/fixtures/' + path.basename(name, '.png') + '.rgba', img.data);
      });
});
