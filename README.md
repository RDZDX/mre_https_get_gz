## mre_https_get.vxp

https get demonstration on MRE platform mobile phones, including Nokia S30+ devices.

## Nokia Phone Signing

For use on Nokia mobile phones, the application must be signed using the IMSI code of your SIM card.

More information: https://vxpatch.luxferre.top

## File

- [mre_https_get_gz.vxp](https://rdzdx.github.io/mre_https_get_gz/mre_https_get_gz.vxp)

| URL                                   | Content-Encoding | Final filename     |
| ------------------------------------- | ---------------- | ------------------ |
| `https://www.website.com/picture.jpg` | none             | `e:\picture.jpg`   |
| `https://www.website.com/picture.jpg` | gzip             | `e:\picture.gz`    |
| `https://www.website.com/file.bin`    | none             | `e:\file.bin`      |
| `https://www.website.com/file.bin`    | gzip             | `e:\file.gz`       |
| `https://www.website.com/`            | gzip             | `e:\download.gz`   |
| `https://www.website.com/`            | none             | `e:\download.html` |

python pycert_bearssl.py convert \
    isrg-root-x1.pem \
    digicert-global-root-g2.pem \
    digicert-global-root-g3.pem \
    globalsign-root-ca.pem \
    Go_Daddy_Root_Certificate+Authority_-_G2.pem \
    gtsr1.pem \
    SSL.com_TLS_ECC_Root_CA_2022.pem \
    > certificates.h
