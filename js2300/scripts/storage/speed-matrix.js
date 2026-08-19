// unifrog: mode=extension

var ret = JS2300.system.action("developer:storage_speed_matrix");
JS2300.log("storage speed matrix ret=" + ret);
if (ret <= 0)
  throw new Error("storage speed matrix failed");
