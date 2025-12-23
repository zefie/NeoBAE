var Module = {
  locateFile: function(path, prefix) {
    if (path.endsWith(".wasm")) {
      return "/testing/" + path;
    }
    return prefix + path;
  }
};

