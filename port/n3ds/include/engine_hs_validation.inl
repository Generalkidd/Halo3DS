/* Included only by the bootstrap build, after the real private conversion
 * routines. Volatile function pointers exercise ARM's indirect-call ABI. */
int halo_engine_hs_cast_tests(void)
{
    hs_typecasting_procedure volatile callback;
    union hs_conversion_result result, input;
#define CAST_CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
    callback = n3ds_hs_data_to_void;
    CAST_CHECK(callback(0x12345678) == 0);
    callback = n3ds_hs_short_to_real;
    result.long_integer = callback(0x1234ff85);
    CAST_CHECK(result.real == -123.f);
    callback = n3ds_hs_long_to_real;
    result.long_integer = callback(-123456);
    CAST_CHECK(result.real == -123456.f);
    callback = n3ds_hs_enum_to_real;
    result.long_integer = callback(2);
    CAST_CHECK(result.real == 3.f);
    input.real = -12.75f;
    callback = n3ds_hs_real_to_long;
    CAST_CHECK(callback(input.long_integer) == -12);
    callback = n3ds_hs_real_to_short;
    result.long_integer = callback(input.long_integer);
    CAST_CHECK(result.short_integer == -12);
    callback = n3ds_hs_long_to_short;
    CAST_CHECK(callback(0x12348001) == 0x12348001);
    /* Preserve original helper semantics while checking register/value layout. */
    callback = n3ds_hs_long_to_boolean;
    result.long_integer = callback(0); CAST_CHECK(result.boolean == 1);
    result.long_integer = callback(256); CAST_CHECK(result.boolean == 0);
    callback = n3ds_hs_short_to_boolean;
    result.long_integer = callback(0x12340000); CAST_CHECK(result.boolean == 1);
    result.long_integer = callback(0x12340001); CAST_CHECK(result.boolean == 0);
    callback = n3ds_hs_string_to_boolean;
    input.string = ""; CAST_CHECK(callback(input.long_integer) == 1);
    input.string = "halo"; CAST_CHECK(callback(input.long_integer) == 0);
    return 0;
#undef CAST_CHECK
}
