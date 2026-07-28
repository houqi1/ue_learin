# Niagara Python 崩溃禁令（UE 5.8）

## 崩溃根因（本次 callstack）

```
Assertion failed: IsValid()  SharedPointer.h:1133
UNiagaraPythonEmitter::GetObject()
```

`UNiagaraPythonEmitter` **不是**普通可 `unreal.NiagaraPythonEmitter()` 新建的发射器包装。

它只在 **Emitter 升级脚本上下文**里由引擎 `Init(TSharedRef<FNiagaraEmitterHandleViewModel>)` 注入有效的 `EmitterViewModel` 后才能用。

空构造后调用：

- `get_object()`
- `get_modules()` / `get_module()` / `has_module()`（内部也会走到无效 ViewModel）
- `get_properties()` / `set_properties()`

都会解引用无效 `TSharedPtr`，**直接 assert 崩编辑器**（不是 Python 异常，无法 try/except）。

源码：

```cpp
// UpgradeNiagaraScriptResults.cpp
UNiagaraEmitter* UNiagaraPythonEmitter::GetObject()
{
    return EmitterViewModel->GetEmitterViewModel()->GetEmitter().Emitter;
}
```

## 禁止写法

```python
npe = unreal.NiagaraPythonEmitter()   # 空壳
npe.get_object()                      # 必崩
npe.get_modules()                     # 必崩
```

## 相对安全的访问方式

```python
# 直接找子对象，不要走 NiagaraPythonEmitter
em = unreal.find_object(
    None,
    "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D:FluidGrid_0",
)
# 再 get_editor_property / find_object 子路径
```

## 本工程约定

- **Stage Custom HLSL 优先 MCP**，不跑会触碰 `NiagaraPythonEmitter` 的脚本。
- 诊断脚本只允许 `find_object` + `get_editor_property` 白名单。
- 任何 Python 脚本顶部必须声明：禁止空 `NiagaraPythonEmitter`。
