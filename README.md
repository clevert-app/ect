# ect - Efficient Compression Tool

The `ect` is a optimizer for GZIP, ZIP, PNG and more formats. This is a friendly community fork of origin [fhanau ect](https://github.com/fhanau/Efficient-Compression-Tool).

## Usage and download

https://github.com/clevert-app/ect/releases

## Performance

> Compress enwik8 to gzip. Use x86_64, AMD Ryzen R5 5600U, boost off, fixed 2.3 GHz. Debian 13.

| name        | size     | time    |
| ----------- | -------- | ------- |
| ect -3      | 35014536 | 19.527  |
| ect -6      | 34943936 | 48.392  |
| gzip -9     | 36445248 | 9.040   |
| zopfli -i1  | 35102371 | 107.144 |
| zopfli -i5  | 34983757 | 163.452 |
| zopfli -i10 | 34971051 | 232.151 |
| zopfli -i15 | 34966078 | 301.015 |
| ect_old -3  | 35014543 | 21.631  |
| ect_old -6  | 34943943 | 55.568  |

- ect = this repo version `x.x.x` , ect_old = [origin fhanau ect `e711c5e`](https://github.com/fhanau/Efficient-Compression-Tool/tree/e711c5ea9d725d02db546ce926a66b91b68ecb3a) , zopfli = [google zopfli `df1517d`](https://github.com/google/zopfli/tree/df1517dd8518113a245e4d203a04b8a0e02ab493) , gzip = [gnu gzip `1.13`](https://deb.debian.org/debian/pool/main/g/gzip/gzip_1.13-1+deb13u1_amd64.deb)

<!--- `arm64` = Mac mini M1.-->

<details>

</details>

## License

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0.txt)

## Thanks

- https://github.com/fhanau/Efficient-Compression-Tool
- https://github.com/JayXon/Leanify
- https://github.com/google/zopfli
