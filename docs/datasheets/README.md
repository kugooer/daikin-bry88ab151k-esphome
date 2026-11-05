# 规格书原件

| 文件 | 说明 |
|---|---|
| `ZE08C-formaldehyde-module-datasheet-CN.pdf` | 炜盛（Winsen）《黑-ZE08C 甲醛检测模块技术规格书》，4 页，3.6 MB |

```
SHA256  9d61defba423561004374a91fdd929bc60e8c6c4b15b890723fc86348f678122
```

收录目的：本项目与规格书存在两处**实测与纸面不一致**的地方，留原件便于比对：

1. **B4 B5 满量程字段**——规格书示例为 `03 E8`（1000 µg/m³），实测模块自报 `07 D0`（2000 µg/m³）。详见 [`../02-ze08c-protocol.md`](../02-ze08c-protocol.md) 第 4 节。
2. **端口顺序**——规格书为 `1=负 2=正 3=A(RX) 4=B(TX)`，与常见「VCC-GND-TX-RX」模块相反。详见 [`../01-hardware-wiring.md`](../01-hardware-wiring.md) 第 1 节。

> 版权归厂商所有，此处仅作技术分析与问题复现的引用，不作商业分发。
