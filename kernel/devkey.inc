#define ECC_MAX_SIZE 8

typedef struct ecc_point
{
    uint32_t x[ECC_MAX_SIZE];
    uint32_t y[ECC_MAX_SIZE];
} ecc_point;

typedef struct ecc_curve
{
  int size;
  uint32_t n[ECC_MAX_SIZE];
  uint32_t p[ECC_MAX_SIZE];
  struct ecc_point g;
} ecc_curve;

static ecc_curve curve_128 = {
  .size = 4,
  .n = { 0x9038a115ul, 0x75a30d1bul, 0x00000000ul, 0xfffffffeul },
  .p = { 0xfffffffful, 0xfffffffful, 0xfffffffful, 0xfffffffdul },
  .g = {
    .x = { 0xa52c5b86ul, 0x0c28607cul, 0x8b899b2dul, 0x161ff752ul },
    .y = { 0xdded7a83ul, 0xc02da292ul, 0x5bafeb13ul, 0xcf5ac839ul }
  }
};

static ecc_curve curve_256 = {
  .size = 8,
  .n = { 0xfc632551ul, 0xf3b9cac2ul, 0xa7179e84ul, 0xbce6faadul, 0xfffffffful, 0xfffffffful, 0x00000000ul, 0xfffffffful },
  .p = { 0xfffffffful, 0xfffffffful, 0xfffffffful, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000001ul, 0xfffffffful },
  .g = {
    .x = { 0xd898c296ul, 0xf4a13945ul, 0x2deb33a0ul, 0x77037d81ul, 0x63a440f2ul, 0xf8bce6e5ul, 0xe12c4247ul, 0x6b17d1f2ul },
    .y = { 0x37bf51f5ul, 0xcbb64068ul, 0x6b315eceul, 0x2bce3357ul, 0x7c0f9e16ul, 0x8ee7eb4aul, 0xfe1a7f9bul, 0x4fe342e2ul }
  }
};

static bool bigint_is_zero(uint32_t *bi, int size)
{
  int i;
  for (i = 0; i < size; i++)
  {
    if (bi[i])
      return false;
  }
  return true;
}

static uint32_t bigint_test_bit(uint32_t *bi, int bit, int size)
{
  return (bi[bit / 32] & ((uint32_t)1 << (bit % 32)));
}

static int bigint_cmp(uint32_t *left, uint32_t *right, int size)
{
  int i;
  for (i = size - 1; i >= 0; i--)
  {
    if (left[i] > right[i])
      return 1;
    else if (right[i] > left[i])
      return -1;
  }
  return 0;
}

static void bigint_set(uint32_t *dest, uint32_t *src, int size)
{
  memcpy(dest, src, size * 4);
}

static void bigint_clear(uint32_t *bi, int size)
{
  memset(bi, 0, size * 4);
}

static uint32_t bigint_lshift(uint32_t *bi, int size)
{
  uint32_t carry = 0;
  int i;

  for (i = 0; i < size; i++)
  {
    uint32_t temp = bi[i];

    bi[i] = (temp << 1) | carry;
    carry = temp >> 31;
  }

  return carry;
}

static void bigint_rshift(uint32_t *bi, int size)
{
  uint32_t *end = bi;
  uint32_t carry = 0;

  bi += size;
  while (bi-- > end)
  {
    uint32_t temp = *bi;
    *bi = (temp >> 1) | carry;
    carry = temp << 31;
  }
}

static uint32_t bigint_add(uint32_t *res, uint32_t *left, uint32_t *right, int size)
{
  uint32_t carry = 0;
  int i;

  for(i = 0; i < size; i++)
  {
    uint32_t sum = left[i] + right[i] + carry;
    if (sum != left[i])
      carry = (sum < left[i]);
    res[i] = sum;
  }
  return carry;
}

static uint32_t bigint_sub(uint32_t *res, uint32_t *left, uint32_t *right, int size)
{
  uint32_t borrow = 0;
  int i;

  for(i = 0; i < size; i++)
  {
    uint32_t diff = left[i] - right[i] - borrow;
    if (diff != left[i])
      borrow = (diff > left[i]);
    res[i] = diff;
  }
  return borrow;
}

#define bigint_is_even(bi, size) (!(bi[0] & 1))

static void bigint_mod_inv(uint32_t *res, uint32_t *in, uint32_t *mod, int size)
{
  uint32_t a[ECC_MAX_SIZE], b[ECC_MAX_SIZE], u[ECC_MAX_SIZE], v[ECC_MAX_SIZE];
  uint32_t carry;
  int cmp;

  bigint_set(a, in, size);
  bigint_set(b, mod, size);
  bigint_clear(u, size);
  u[0] = 1;
  bigint_clear(v, size);

  while ((cmp = bigint_cmp(a, b, size)) != 0)
  {
    carry = 0;
    if (bigint_is_even(a, size))
    {
      bigint_rshift(a, size);

      if (!bigint_is_even(u, size))
        carry = bigint_add(u, u, mod, size);

      bigint_rshift(u, size);

      if (carry)
        u[size - 1] |= 0x80000000ul;
    }
    else if (bigint_is_even(b, size))
    {
      bigint_rshift(b, size);

      if (!bigint_is_even(v, size))
        carry = bigint_add(v, v, mod, size);

      bigint_rshift(v, size);

      if (carry)
        v[size - 1] |= 0x80000000ul;
    }
    else if (cmp > 0)
    {
      bigint_sub(a, a, b, size);

      bigint_rshift(a, size);

      if (bigint_cmp(u, v, size) < 0)
        bigint_add(u, u, mod, size);

      bigint_sub(u, u, v, size);

      if (!bigint_is_even(u, size))
        carry = bigint_add(u, u, mod, size);

      bigint_rshift(u, size);

      if (carry)
        u[size - 1] |= 0x80000000ul;
    }
    else
    {
      bigint_sub(b, b, a, size);

      bigint_rshift(b, size);

      if (bigint_cmp(v, u, size) < 0)
        bigint_add(v, v, mod, size);

      bigint_sub(v, v, u, size);

      if (!bigint_is_even(v, size))
        carry = bigint_add(v, v, mod, size);

      bigint_rshift(v, size);

      if (carry)
        v[size - 1] |= 0x80000000ul;
    }
  }

  bigint_set(res, u, size);
}

static void bigint_mult(uint32_t *res, uint32_t *left, uint32_t *right, int size)
{
  uint64_t r01 = 0;
  uint32_t r2 = 0;
  int i, k;

  for (k = 0; k < size * 2 - 1; k++)
  {
    int min = (k < size ? 0 : (k + 1) - size);
    for (i = min; i <= k && i < size; i++)
    {
      uint64_t product = (uint64_t)left[i] * right[k - i];
      r01 += product;
        r2 += (r01 < product);
    }
    res[k] = (uint32_t)r01;
    r01 = (r01 >> 32) | (((uint64_t)r2) << 32);
    r2 = 0;
  }

  res[size * 2 - 1] = (uint32_t)r01;
}

static int bigint_num_dwords(uint32_t *bi, int size)
{
  int i;

  for(i = size - 1; i >= 0 && bi[i] == 0; i--)
  {
  }

  return i + 1;
}

static int bigint_num_bits(uint32_t *bi, int size)
{
  int i;
  int dwords;
  uint32_t tmp;

  dwords = bigint_num_dwords(bi, size);

  if (dwords == 0)
    return 0;

  tmp = bi[dwords - 1];
  for(i = 0; tmp; i++)
  {
    tmp >>= 1;
  }

  return ((dwords - 1) * 32 + i);
}

static void bigint_mod_mult(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod, int size)
{
  uint32_t product[ECC_MAX_SIZE * 2];
  uint32_t mod_m[ECC_MAX_SIZE * 2];
  uint32_t tmp[ECC_MAX_SIZE * 2];
  uint32_t *v[2] = { tmp, product };
  uint32_t carry = 0;
  int i;
  int shift = 32 * 2 * size - bigint_num_bits(mod, size);
  int dword_shift = shift / 32;
  int bit_shift = shift % 32;

  bigint_mult(product, left, right, size);

  bigint_clear(mod_m, size * 2);

  if (bit_shift > 0)
  {
    for (i = 0; i < size; i++)
    {
      mod_m[dword_shift + i] = (mod[i] << bit_shift) | carry;
      carry = mod[i] >> (32 - bit_shift);
    }
  }
  else
  {
    bigint_set(mod_m + dword_shift, mod, size);
  }

  for (i = 1; shift >= 0; shift--) {
    uint32_t borrow = 0;
    unsigned int j;

    for (j = 0; j < size * 2; ++j)
    {
      uint32_t diff = v[i][j] - mod_m[j] - borrow;

      if (diff != v[i][j])
        borrow = (diff > v[i][j]);

      v[1 - i][j] = diff;
    }

    i = !(i ^ borrow);
    bigint_rshift(mod_m, size);
    mod_m[size - 1] |= mod_m[size] << 31;
    bigint_rshift(mod_m + size, size);
  }

  bigint_set(res, v[i], size);
}

static void bigint_mod_add(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod, int size)
{
  uint32_t carry = bigint_add(res, left, right, size);

  if(carry || bigint_cmp(res, mod, size) >= 0)
    bigint_sub(res, res, mod, size);
}

static void bigint_mod_sub(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod, int size)
{
  uint32_t borrow = bigint_sub(res, left, right, size);

  if (borrow)
    bigint_add(res, res, mod, size);
}

static void bigint_mod_mult_fast_128(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod)
{
  uint8_t product[32];
  uint8_t tmp[16];
  int carry;

  bigint_mult((uint32_t*)product, left, right, 4);

  bigint_set(res, (uint32_t*)product, 4);

  tmp[0]  = product[16];
  tmp[1]  = product[17];
  tmp[2]  = product[18];
  tmp[3]  = product[19];
  tmp[4]  = product[20];
  tmp[5]  = product[21];
  tmp[6]  = product[22];
  tmp[7]  = product[23];
  tmp[8]  = product[24];
  tmp[9]  = product[25];
  tmp[10] = product[26];
  tmp[11] = product[27];
  tmp[12] = (product[28] &  1) | (product[16] << 1);
  tmp[13] = (product[16] >> 7) | (product[17] << 1);
  tmp[14] = (product[17] >> 7) | (product[18] << 1);
  tmp[15] = (product[18] >> 7) | (product[19] << 1);
  carry = bigint_add(res, res, (uint32_t*)tmp, 4);

  tmp[0]  = (product[19] >> 7) | (product[20] << 1);
  tmp[1]  = (product[20] >> 7) | (product[21] << 1);
  tmp[2]  = (product[21] >> 7) | (product[22] << 1);
  tmp[3]  = (product[22] >> 7) | (product[23] << 1);
  tmp[4]  = (product[23] >> 7) | (product[24] << 1);
  tmp[5]  = (product[24] >> 7) | (product[25] << 1);
  tmp[6]  = (product[25] >> 7) | (product[26] << 1);
  tmp[7]  = (product[26] >> 7) | (product[27] << 1);
  tmp[8]  = (product[27] >> 7) | (product[28] << 1);
  tmp[9]  = (product[28] >> 7) | (product[29] << 1);
  tmp[10] = (product[29] >> 7) | (product[30] << 1);
  tmp[11] = (product[30] >> 7) | (product[31] << 1);
  tmp[12] = (product[31] >> 7) | ((product[19] & 0x80) >> 6) | (product[20] << 2);
  tmp[13] = (product[20] >> 6) | (product[21] << 2);
  tmp[14] = (product[21] >> 6) | (product[22] << 2);
  tmp[15] = (product[22] >> 6) | (product[23] << 2);
  carry += bigint_add(res, res, (uint32_t*)tmp, 4);

  tmp[0]  = (product[23] >> 6) | (product[24] << 2);
  tmp[1]  = (product[24] >> 6) | (product[25] << 2);
  tmp[2]  = (product[25] >> 6) | (product[26] << 2);
  tmp[3]  = (product[26] >> 6) | (product[27] << 2);
  tmp[4]  = (product[27] >> 6) | (product[28] << 2);
  tmp[5]  = (product[28] >> 6) | (product[29] << 2);
  tmp[6]  = (product[29] >> 6) | (product[30] << 2);
  tmp[7]  = (product[30] >> 6) | (product[31] << 2);
  tmp[8]  = (product[31] >> 6);
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = ((product[23] & 0xC0) >> 5) | (product[24] << 3);
  tmp[13] = (product[24] >> 5) | (product[25] << 3);
  tmp[14] = (product[25] >> 5) | (product[26] << 3);
  tmp[15] = (product[26] >> 5) | (product[27] << 3);
  carry += bigint_add(res, res, (uint32_t*)tmp, 4);

  tmp[0]  = (product[27] >> 5) | (product[28] << 3);
  tmp[1]  = (product[28] >> 5) | (product[29] << 3);
  tmp[2]  = (product[29] >> 5) | (product[30] << 3);
  tmp[3]  = (product[30] >> 5) | (product[31] << 3);
  tmp[4]  = (product[31] >> 5);
  tmp[5]  = 0;
  tmp[6]  = 0;
  tmp[7]  = 0;
  tmp[8]  = 0;
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = ((product[27] & 0xE0) >> 4) | (product[28] << 4);
  tmp[13] = (product[28] >> 4) | (product[29] << 4);
  tmp[14] = (product[29] >> 4) | (product[30] << 4);
  tmp[15] = (product[30] >> 4) | (product[31] << 4);
  carry += bigint_add(res, res, (uint32_t*)tmp, 4);

  tmp[0]  = (product[31] >> 4);
  tmp[1]  = 0;
  tmp[2]  = 0;
  tmp[3]  = 0;
  tmp[4]  = 0;
  tmp[5]  = 0;
  tmp[6]  = 0;
  tmp[7]  = 0;
  tmp[8]  = 0;
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = (product[28] & 0xFE);
  tmp[13] = product[29];
  tmp[14] = product[30];
  tmp[15] = product[31];
  carry += bigint_add(res, res, (uint32_t*)tmp, 4);

  tmp[0]  = 0;
  tmp[1]  = 0;
  tmp[2]  = 0;
  tmp[3]  = 0;
  tmp[4]  = 0;
  tmp[5]  = 0;
  tmp[6]  = 0;
  tmp[7]  = 0;
  tmp[8]  = 0;
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = ((product[31] & 0xF0) >> 3);
  tmp[13] = 0;
  tmp[14] = 0;
  tmp[15] = 0;
  carry += bigint_add(res, res, (uint32_t*)tmp, 4);

  while(carry || bigint_cmp(res, mod, 4) >= 0)
  {
    carry -= bigint_sub(res, res, mod, 4);
  }
}

static void bigint_mod_mult_fast_256(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod)
{
  uint8_t product[64];
  uint8_t tmp[32];
  int carry;

  bigint_mult((uint32_t*)product, left, right, 8);

  bigint_set(res, (uint32_t*)product, 8);

  tmp[0]  = 0;
  tmp[1]  = 0;
  tmp[2]  = 0;
  tmp[3]  = 0;
  tmp[4]  = 0;
  tmp[5]  = 0;
  tmp[6]  = 0;
  tmp[7]  = 0;
  tmp[8]  = 0;
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = product[44];
  tmp[13] = product[45];
  tmp[14] = product[46];
  tmp[15] = product[47];
  tmp[16] = product[48];
  tmp[17] = product[49];
  tmp[18] = product[50];
  tmp[19] = product[51];
  tmp[20] = product[52];
  tmp[21] = product[53];
  tmp[22] = product[54];
  tmp[23] = product[55];
  tmp[24] = product[56];
  tmp[25] = product[57];
  tmp[26] = product[58];
  tmp[27] = product[59];
  tmp[28] = product[60];
  tmp[29] = product[61];
  tmp[30] = product[62];
  tmp[31] = product[63];
  carry = bigint_lshift((uint32_t*)tmp, 8);
  carry += bigint_add((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[12] = product[48];
  tmp[13] = product[49];
  tmp[14] = product[50];
  tmp[15] = product[51];
  tmp[16] = product[52];
  tmp[17] = product[53];
  tmp[18] = product[54];
  tmp[19] = product[55];
  tmp[20] = product[56];
  tmp[21] = product[57];
  tmp[22] = product[58];
  tmp[23] = product[59];
  tmp[24] = product[60];
  tmp[25] = product[61];
  tmp[26] = product[62];
  tmp[27] = product[63];
  tmp[28] = 0;
  tmp[29] = 0;
  tmp[30] = 0;
  tmp[31] = 0;
  carry += bigint_lshift((uint32_t*)tmp, 8);
  carry += bigint_add((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[32];
  tmp[1]  = product[33];
  tmp[2]  = product[34];
  tmp[3]  = product[35];
  tmp[4]  = product[36];
  tmp[5]  = product[37];
  tmp[6]  = product[38];
  tmp[7]  = product[39];
  tmp[8]  = product[40];
  tmp[9]  = product[41];
  tmp[10] = product[42];
  tmp[11] = product[43];
  tmp[12] = 0;
  tmp[13] = 0;
  tmp[14] = 0;
  tmp[15] = 0;
  tmp[16] = 0;
  tmp[17] = 0;
  tmp[18] = 0;
  tmp[19] = 0;
  tmp[20] = 0;
  tmp[21] = 0;
  tmp[22] = 0;
  tmp[23] = 0;
  tmp[24] = product[56];
  tmp[25] = product[57];
  tmp[26] = product[58];
  tmp[27] = product[59];
  tmp[28] = product[60];
  tmp[29] = product[61];
  tmp[30] = product[62];
  tmp[31] = product[63];
  carry += bigint_add((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[36];
  tmp[1]  = product[37];
  tmp[2]  = product[38];
  tmp[3]  = product[39];
  tmp[4]  = product[40];
  tmp[5]  = product[41];
  tmp[6]  = product[42];
  tmp[7]  = product[43];
  tmp[8]  = product[44];
  tmp[9]  = product[45];
  tmp[10] = product[46];
  tmp[11] = product[47];
  tmp[12] = product[52];
  tmp[13] = product[53];
  tmp[14] = product[54];
  tmp[15] = product[55];
  tmp[16] = product[56];
  tmp[17] = product[57];
  tmp[18] = product[58];
  tmp[19] = product[59];
  tmp[20] = product[60];
  tmp[21] = product[61];
  tmp[22] = product[62];
  tmp[23] = product[63];
  tmp[24] = product[52];
  tmp[25] = product[53];
  tmp[26] = product[54];
  tmp[27] = product[55];
  tmp[28] = product[32];
  tmp[29] = product[33];
  tmp[30] = product[34];
  tmp[31] = product[35];
  carry += bigint_add((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[44];
  tmp[1]  = product[45];
  tmp[2]  = product[46];
  tmp[3]  = product[47];
  tmp[4]  = product[48];
  tmp[5]  = product[49];
  tmp[6]  = product[50];
  tmp[7]  = product[51];
  tmp[8]  = product[52];
  tmp[9]  = product[53];
  tmp[10] = product[54];
  tmp[11] = product[55];
  tmp[12] = 0;
  tmp[13] = 0;
  tmp[14] = 0;
  tmp[15] = 0;
  tmp[16] = 0;
  tmp[17] = 0;
  tmp[18] = 0;
  tmp[19] = 0;
  tmp[20] = 0;
  tmp[21] = 0;
  tmp[22] = 0;
  tmp[23] = 0;
  tmp[24] = product[32];
  tmp[25] = product[33];
  tmp[26] = product[34];
  tmp[27] = product[35];
  tmp[28] = product[40];
  tmp[29] = product[41];
  tmp[30] = product[42];
  tmp[31] = product[43];
  carry -= bigint_sub((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[48];
  tmp[1]  = product[49];
  tmp[2]  = product[50];
  tmp[3]  = product[51];
  tmp[4]  = product[52];
  tmp[5]  = product[53];
  tmp[6]  = product[54];
  tmp[7]  = product[55];
  tmp[8]  = product[56];
  tmp[9]  = product[57];
  tmp[10] = product[58];
  tmp[11] = product[59];
  tmp[12] = product[60];
  tmp[13] = product[61];
  tmp[14] = product[62];
  tmp[15] = product[63];
  tmp[16] = 0;
  tmp[17] = 0;
  tmp[18] = 0;
  tmp[19] = 0;
  tmp[20] = 0;
  tmp[21] = 0;
  tmp[22] = 0;
  tmp[23] = 0;
  tmp[24] = product[36];
  tmp[25] = product[37];
  tmp[26] = product[38];
  tmp[27] = product[39];
  tmp[28] = product[44];
  tmp[29] = product[45];
  tmp[30] = product[46];
  tmp[31] = product[47];
  carry -= bigint_sub((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[52];
  tmp[1]  = product[53];
  tmp[2]  = product[54];
  tmp[3]  = product[55];
  tmp[4]  = product[56];
  tmp[5]  = product[57];
  tmp[6]  = product[58];
  tmp[7]  = product[59];
  tmp[8]  = product[60];
  tmp[9]  = product[61];
  tmp[10] = product[62];
  tmp[11] = product[63];
  tmp[12] = product[32];
  tmp[13] = product[33];
  tmp[14] = product[34];
  tmp[15] = product[35];
  tmp[16] = product[36];
  tmp[17] = product[37];
  tmp[18] = product[38];
  tmp[19] = product[39];
  tmp[20] = product[40];
  tmp[21] = product[41];
  tmp[22] = product[42];
  tmp[23] = product[43];
  tmp[24] = 0;
  tmp[25] = 0;
  tmp[26] = 0;
  tmp[27] = 0;
  tmp[28] = product[48];
  tmp[29] = product[49];
  tmp[30] = product[50];
  tmp[31] = product[51];
  carry -= bigint_sub((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  tmp[0]  = product[56];
  tmp[1]  = product[57];
  tmp[2]  = product[58];
  tmp[3]  = product[59];
  tmp[4]  = product[60];
  tmp[5]  = product[61];
  tmp[6]  = product[62];
  tmp[7]  = product[63];
  tmp[8]  = 0;
  tmp[9]  = 0;
  tmp[10] = 0;
  tmp[11] = 0;
  tmp[12] = product[36];
  tmp[13] = product[37];
  tmp[14] = product[38];
  tmp[15] = product[39];
  tmp[16] = product[40];
  tmp[17] = product[41];
  tmp[18] = product[42];
  tmp[19] = product[43];
  tmp[20] = product[44];
  tmp[21] = product[45];
  tmp[22] = product[46];
  tmp[23] = product[47];
  tmp[24] = 0;
  tmp[25] = 0;
  tmp[26] = 0;
  tmp[27] = 0;
  tmp[28] = product[52];
  tmp[29] = product[53];
  tmp[30] = product[54];
  tmp[31] = product[55];
  carry -= bigint_sub((uint32_t*)res, (uint32_t*)res, (uint32_t*)tmp, 8);

  if(carry < 0)
  {
    do
    {
      carry += bigint_add((uint32_t*)res, (uint32_t*)res, mod, 8);
    } while(carry < 0);
  }
  else
  {
    while(carry || bigint_cmp(mod, (uint32_t*)res, 8) != 1)
    {
      carry -= bigint_sub((uint32_t*)res, (uint32_t*)res, mod, 8);
    }
  }
}

static void bigint_mod_mult_fast(uint32_t *res, uint32_t *left, uint32_t *right, uint32_t *mod, int size)
{
  switch (size)
  {
    case 4:
      bigint_mod_mult_fast_128(res, left, right, mod);
      break;
    case 8:
      bigint_mod_mult_fast_256(res, left, right, mod);
      break;
  }
}

static void xycz_add(struct ecc_point *p1, struct ecc_point *p2, uint32_t *mod, int size)
{
  uint32_t tmp[ECC_MAX_SIZE];

  bigint_mod_sub(tmp, p2->x, p1->x, mod, size);
  bigint_mod_mult_fast(tmp, tmp, tmp, mod, size);
  bigint_mod_mult_fast(p1->x, p1->x, tmp, mod, size);
  bigint_mod_mult_fast(p2->x, p2->x, tmp, mod, size);
  bigint_mod_sub(p2->y, p2->y, p1->y, mod, size);
  bigint_mod_mult_fast(tmp, p2->y, p2->y, mod, size);

  bigint_mod_sub(tmp, tmp, p1->x, mod, size);
  bigint_mod_sub(tmp, tmp, p2->x, mod, size);
  bigint_mod_sub(p2->x, p2->x, p1->x, mod, size);
  bigint_mod_mult_fast(p1->y, p1->y, p2->x, mod, size);
  bigint_mod_sub(p2->x, p1->x, tmp, mod, size);
  bigint_mod_mult_fast(p2->y, p2->y, p2->x, mod, size);
  bigint_mod_sub(p2->y, p2->y, p1->y, mod, size);

  bigint_set(p2->x, tmp, size);
}

static void apply_z(struct ecc_point *p, uint32_t *z, uint32_t *mod, int size)
{
  uint32_t tmp[ECC_MAX_SIZE];

  bigint_mod_mult_fast(tmp, z, z, mod, size);
  bigint_mod_mult_fast(p->x, p->x, tmp, mod, size);
  bigint_mod_mult_fast(tmp, tmp, z, mod, size);
  bigint_mod_mult_fast(p->y, p->y, tmp, mod, size);
}

static void point_double_jacobian(struct ecc_point *p, uint32_t *z, uint32_t *mod, int size)
{
  uint32_t t4[ECC_MAX_SIZE];
  uint32_t t5[ECC_MAX_SIZE];

  if (bigint_is_zero(z, size))
    return;

  bigint_mod_mult_fast(t4, p->y, p->y, mod, size);   /* t4 = y1^2 */
  bigint_mod_mult_fast(t5, p->x, t4, mod, size); /* t5 = x1*y1^2 = A */
  bigint_mod_mult_fast(t4, t4, t4, mod, size);   /* t4 = y1^4 */
  bigint_mod_mult_fast(p->y, p->y, z, mod, size); /* t2 = y1*z1 = z3 */
  bigint_mod_mult_fast(z, z, z, mod, size);   /* t3 = z1^2 */

  bigint_mod_add(p->x, p->x, z, mod, size); /* t1 = x1 + z1^2 */
  bigint_mod_add(z, z, z, mod, size); /* t3 = 2*z1^2 */
  bigint_mod_sub(z, p->x, z, mod, size); /* t3 = x1 - z1^2 */
  bigint_mod_mult_fast(p->x, p->x, z, mod, size);    /* t1 = x1^2 - z1^4 */

  bigint_mod_add(z, p->x, p->x, mod, size); /* t3 = 2*(x1^2 - z1^4) */
  bigint_mod_add(p->x, p->x, z, mod, size); /* t1 = 3*(x1^2 - z1^4) */
  if (bigint_test_bit(p->x, 0, size))
  {
    uint32_t carry = bigint_add(p->x, p->x, mod, size);
    bigint_rshift(p->x, size);
    p->x[size - 1] |= carry << 31;
  }
  else
  {
    bigint_rshift(p->x, size);
  }
                     /* t1 = 3/2*(x1^2 - z1^4) = B */

  bigint_mod_mult_fast(z, p->x, p->x, mod, size);      /* t3 = B^2 */
  bigint_mod_sub(z, z, t5, mod, size); /* t3 = B^2 - A */
  bigint_mod_sub(z, z, t5, mod, size); /* t3 = B^2 - 2A = x3 */
  bigint_mod_sub(t5, t5, z, mod, size); /* t5 = A - x3 */
  bigint_mod_mult_fast(p->x, p->x, t5, mod, size);    /* t1 = B * (A - x3) */
  bigint_mod_sub(t4, p->x, t4, mod, size); /* t4 = B * (A - x3) - y1^4 = y3 */

  bigint_set(p->x, z, size);
  bigint_set(z, p->y, size);
  bigint_set(p->y, t4, size);
}

static void point_mult_shamir(struct ecc_point *res, uint32_t *u1, uint32_t *u2, struct ecc_point *q, struct ecc_curve *curve)
{
  uint32_t z[ECC_MAX_SIZE];
  uint32_t tz[ECC_MAX_SIZE];
  struct ecc_point sum, t;
  struct ecc_point *points[4] = { NULL, &curve->g, q, &sum };
  struct ecc_point *point;
  int numBits;
  int i;

  bigint_set(sum.x, q->x, curve->size);
  bigint_set(sum.y, q->y, curve->size);
  bigint_set(t.x, curve->g.x, curve->size);
  bigint_set(t.y, curve->g.y, curve->size);

  bigint_mod_sub(z, sum.x, t.x, curve->p, curve->size);
  xycz_add(&t, &sum, curve->p, curve->size);
  bigint_mod_inv(z, z, curve->p, curve->size);
  apply_z(&sum, z, curve->p, curve->size);

  numBits = max(bigint_num_bits(u1, curve->size), bigint_num_bits(u2, curve->size)) - 1;

  point = points[(!!bigint_test_bit(u1, numBits, curve->size)) | ((!!bigint_test_bit(u2, numBits, curve->size)) << 1)];

  bigint_set(res->x, point->x, curve->size);
  bigint_set(res->y, point->y, curve->size);

  bigint_clear(z, curve->size);
  z[0] = 1;

  for (i = numBits - 1; i >= 0; i--)
  {
    point_double_jacobian(res, z, curve->p, curve->size);

    point = points[(!!bigint_test_bit(u1, i, curve->size)) | ((!!bigint_test_bit(u2, i, curve->size)) << 1)];

    if (point)
    {
      bigint_set(t.x, point->x, curve->size);
      bigint_set(t.y, point->y, curve->size);
      apply_z(&t, z, curve->p, curve->size);
      bigint_mod_sub(tz, res->x, t.x, curve->p, curve->size);
      xycz_add(&t, res, curve->p, curve->size);
      bigint_mod_mult_fast(z, z, tz, curve->p, curve->size);
    }
  }

  bigint_mod_inv(z, z, curve->p, curve->size);
  apply_z(res, z, curve->p, curve->size);
}

static bool verify_device_key(uint32_t *pkey, unsigned char *hkey, unsigned char *skey, int bytes)
{
  int size = bytes / 4;
  uint32_t *r = (uint32_t*)skey;
  uint32_t *s = (uint32_t*)(skey + bytes);
  uint32_t u1[ECC_MAX_SIZE], u2[ECC_MAX_SIZE];
  struct ecc_point p, key;
  struct ecc_curve *curve;

  switch (size)
  {
    case 4:
      curve = &curve_128;
      break;
    case 8:
      curve = &curve_256;
      break;
    default:
      return false;
  }

  if (bigint_is_zero(r, curve->size) || bigint_cmp(r, curve->n, curve->size) >= 0 || bigint_is_zero(s, curve->size) || bigint_cmp(s, curve->n, curve->size) >= 0)
    return false;

  bigint_set(key.x, pkey, curve->size);
  bigint_set(key.y, pkey + size, curve->size);

  bigint_mod_mult(u1, (uint32_t*)hkey, s, curve->n, curve->size);

  bigint_mod_mult(u2, r, s, curve->n, curve->size);

  point_mult_shamir(&p, u1, u2, &key, curve);

  if (bigint_cmp(p.x, curve->n, curve->size) > 0)
    bigint_sub(p.x, p.x, curve->n, curve->size);

  return (bigint_cmp(p.x, r, curve->size) == 0);
}
