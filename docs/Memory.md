# Memory API

## 関数一覧

### `void* Alloc(usize size)`
生メモリを確保

### `void Free(void* ptr)`
メモリを解放

### `Result Alloc(usize size, void** outPtr)`
安全版 Alloc
