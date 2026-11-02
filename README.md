# ect - Efficient Compression Tool

The `ect` is a optimizer for GZIP, ZIP, PNG and more formats. This is a friendly community fork of origin [fhanau ect](https://github.com/fhanau/Efficient-Compression-Tool).

## Usage and download

https://github.com/clevert-app/ect/releases

## Performance

> Compress enwik8 to gzip. Use x86_64, AMD Ryzen R5 5600U, boost off, fixed 2.3 GHz. Debian 13.

| name           | size     | time   |
| -------------- | -------- | ------ |
| gzip -9        | 36445248 | 9.04   |
| libdeflate -9  | 36244263 | 4.29   |
| libdeflate -12 | 35103255 | 20.72  |
| ect -3         | 35014536 | 19.52  |
| ect -6         | 34943936 | 48.39  |
| ect_fhanau -3  | 35014543 | 21.63  |
| ect_fhanau -6  | 34943943 | 55.56  |
| zopfli -i1     | 35102371 | 107.14 |
| zopfli -i5     | 34983757 | 163.45 |
| zopfli -i10    | 34971051 | 232.15 |
| zopfli -i15    | 34966078 | 301.01 |
| zopfli_krzy    | 35001660 | 245.79 |

- ect = this repo version `2.0.1-alpha.2`
- ect_fhanau = [origin fhanau ect `e711c5e`](https://github.com/fhanau/Efficient-Compression-Tool/tree/e711c5ea9d725d02db546ce926a66b91b68ecb3a)
- zopfli = [google zopfli `df1517d`](https://github.com/google/zopfli/tree/df1517dd8518113a245e4d203a04b8a0e02ab493)
- zopfli_krzy = [krzymod zopfli `24.10.2`](https://github.com/MrKrzYch00/zopfli/releases/tag/v24.10.2)
- gzip = [gnu gzip `1.13`](https://deb.debian.org/debian/pool/main/g/gzip/gzip_1.13-1+deb13u1_amd64.deb)
- libdeflate = [libdeflate `1.26`](https://github.com/ebiggers/libdeflate/releases/tag/v1.26)

<details>

todo: `arm64` - Mac mini M1.

```sh
hyperfine --metrics time_wall_clock:s,memory_peak_resident --runs 2 --prepare "rm -rf enwik8.gz" -- "libdeflate-gzip -12 -k enwik8"
```

</details>

## License

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0.txt)

## Thanks

- https://github.com/fhanau/Efficient-Compression-Tool
- https://github.com/JayXon/Leanify
- https://github.com/MrKrzYch00/zopfli
- https://github.com/google/zopfli
