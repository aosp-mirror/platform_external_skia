### Compilation failed:

error: 14: layout qualifier 'sampler' is not permitted here
layout(metal, sampler=0) readonly texture2D rtexture3;              // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
error: 15: layout qualifier 'sampler' is not permitted here
layout(metal, sampler=0) writeonly texture2D wtexture3;             // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
error: 16: 'binding' modifier cannot coexist with 'texture'/'sampler'
layout(metal, binding=0, texture=0, sampler=0) sampler2D sampler3;  // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
error: 17: layout qualifier 'texture' is not permitted here
layout(metal, texture=0, sampler=0) ubo2 { float c; };              // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
error: 17: layout qualifier 'sampler' is not permitted here
layout(metal, texture=0, sampler=0) ubo2 { float c; };              // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
error: 18: layout qualifier 'set' is not permitted here
layout(metal, set=0, binding=0) ubo3 { float d; };                  // invalid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
6 errors
