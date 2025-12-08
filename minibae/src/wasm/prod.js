var Module = {
  locateFile: function(path, prefix) {
    if (path.endsWith(".wasm")) {
      return "/player/" + path;
    }
    return prefix + path;
  }
};

