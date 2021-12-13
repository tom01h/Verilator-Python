# Python をテストベンチにして検証する (Verilator 編)

MMAP を使ったプロセス間通信を使って、Python で記述したテストベンチから Verilator の関数を呼び出します。

DPI-C を使った ModelSIM のサンプルは [DPI-Python (github.com)](https://github.com/tom01h/DPI-Python) にあります。

動作モデルが Python で記述してあったり、入力データや期待値データが Python で扱いやすいときには便利です。あと PYNQ を使うなら、うまく書けば検証用パターンを制御ソフトにそのまま使えるかも！

## 実装例

### 概要

今回使う Verilator の関数は 5種類。`top.cpp` 内に定義しています。

```c++
void v_init();
void v_finish();
void v_write(int address, int data);
void v_send(int data[], int size);
void v_receive(int data[], int size);
```

Python で記述したテストベンチから呼び出す関数は以下の関数が対応します。

```
1: c_init
2: c_finish
3: c_write
4: c_send
5: c_receive
```

`top.py` に記述した上記の関数を呼び出すと MMAP を使ったプロセス間通信で `top.cpp` の関数にデータを渡して呼び出します。

MMAP を使ったプロセス間通信は、今回独自に定義しました。

1バイト目はタスクの番号で、それ以降はタスク独自です。Python 側はタスクを呼ぶときにタスク番号をセットして、タスク終了時に C++ 側が 0 をセットするのを待ちます。

### 起動時

最初に Verilog シミュレーションを起動します。`top.cpp` で MMAP を開いて、タスク要求のポーリングを開始します。

準備ができたら、Python 側で MMAP を開いてテストを開始します。

### レジスタライト

例えば 3番のタスクは AXI スレーブライトです。

Python 側の `top.py` では 4バイト目からの 4バイトにアドレス、8バイト目からの 4バイトにデータをセットしてから、0バイト目にタスク番号の 3をセットします。

```python
def c_write(address, data):
    mm[4:8] = address.to_bytes(4, byteorder='little')
    mm[8:12] = data.to_bytes(4, byteorder='little')
    mm[0:1] = b"\3"
    while mm[0:1] != b'\0':
        pass
    return
```

C++ 側の `top.cpp` ではタスクの要求をポーリングして、タスク番号 3がセットされたら 4バイト目に書かれたアドレスと、8バイト目に書かれたデータを引数に関数 `v_write` を呼び出します。関数の実行が終了したら、0バイト目に 0 を書き込みます。

```c++
    while(1){
        if(buf[0] != 0){
            略
            else if(buf[0] == 3){
                union int_char address, data;
                for(int i=0; i<4; i++){
                    address.c[i] = buf[i+4];
                    data.c[i] = buf[i+8];
                }
                v_write(address.i, data.i);
            }
            略
            buf[0] = 0;
        }
    }
```

呼び出された関数は検証対象のインタフェースを操作します。ここでは AXI スレーブライトです。

```c++
void v_write(int address, int data){
    verilator_top->S_AXI_AWADDR  = address;
    verilator_top->S_AXI_WDATA   = data;
    verilator_top->S_AXI_AWVALID = 1;
    verilator_top->S_AXI_WVALID  = 1;
    eval();
    verilator_top->S_AXI_AWVALID = 0;
    verilator_top->S_AXI_WVALID  = 0;
    eval();
}
```

### ストリームリード

5番のタスクは AXI ストリームリードです。戻り値のある例です。

Python 側の `top.py` では 4バイト目からの 4バイトに転送サイズをセットしてから、0バイト目にタスク番号の 5をセットします。(でも使っているのは4バイトだけみたいです…)

タスクが完了する (0バイト目が 0になる) のを待って、4バイト目から先の領域から 4バイト/ワードのデータをサイズで指定されたワード数だけ list にコピーして返します。

```python
def c_receive(num):
    list = [0] * num
    mm[4:8] = num.to_bytes(4, byteorder='little')
    mm[0:1] = b"\5"
    while mm[0:1] != b'\0':
        pass
    list = []
    for i in range(int.from_bytes(mm[4:8], byteorder='little')):
        list.append(int.from_bytes(mm[4*i+8:4*i+12], byteorder='little'))
    return np.array(list)
```

C++ 側の `top.cpp` ではタスク番号 5がセットされたら 4バイト目に書かれたアドレスと、8バイト目に書かれたサイズを引数に 関数 `v_recieve` を呼び出します。関数の実行が終了したら、4バイト目以降にデータを書き込んでから、0バイト目に 0 を書き込みます。

```c++
            else if(buf[0] == 5){
                int array[64];
                union int_char data, size;
                for(int i=0; i<4; i++){
                    size.c[i] = buf[i+4];
                }
                v_receive(array, size.i);
                for(int i=0; i<size.i; i++){
                    data.i = array[i];
                    for(int j=0; j<4; j++){
                        buf[i*4+j+8] = data.c[j];
                    }
                }
            }
```

呼び出された関数は検証対象のインタフェースを操作します。ここでは AXI ストリームスレーブリードです。

```c++
void v_receive(int data[], int size){
    while(verilator_top->M_AXIS_TVALID== 0)
        eval();
    for(int i=0; i<size; i+=1){
        data[i] = verilator_top->M_AXIS_TDATA;
        eval();
    }
    eval();
}
```

## サンプルの実行

### 準備

WSL2 Ubuntu にインストールした Verilator と Python 3 を使います。

- Verilator をインストール
  - 確認済みのバージョン `Verilator 4.210 2021-07-07 rev v4.210-19-gde408a5e`
- Python をインストール

### 実行

`sim` ディレクトリで `make; ./run_verilator.sh` を実行します。`[Sample N output] = [Matix Inpit] dot [sample N Input]T` を計算して返します。

- Verilog シミュレータ起動
- tb.py を python コマンドから起動
  - 初期化 `v_init`
  - Python スクリプトで乱数で行列を作って `v_send`
  - 以下を繰り返す (2回)
    - Python スクリプトで乱数で行列を作って `v_send`
    - Verilog で行列乗算を計算して結果を `v_receive`
    - Python スクリプトで期待値を計算して先の値と比較
  - 終了 `v_finish`
