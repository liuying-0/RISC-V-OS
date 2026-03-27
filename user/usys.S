.global exit
.global write
exit:
    li a7, 1 
    ecall
    ret        


write:
    li a7, 4
    ecall
    ret