#include <cstdio>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>

#include "coat/Function.h"
#include "coat/ControlFlow.h"



struct Table {
#define MEMBERS(x) \
	x(size_t, ncols) \
	x(size_t, nrows) \
	x(uint64_t*, data)

DECLARE(MEMBERS)
#undef MEMBERS

	Table(size_t ncols, size_t nrows) : ncols(ncols), nrows(nrows) {
		data = new uint64_t[ncols * nrows];
	}
	~Table(){
		delete[] data;
	}

	      uint64_t *operator[](size_t col)       { return data + (col * nrows); }
	const uint64_t *operator[](size_t col) const { return data + (col * nrows); }
};

namespace coat {

template<>
struct has_custom_base<Table> : std::true_type {};

template<class CC>
struct StructBase<Struct<CC,Table>> {
	Ptr<CC,Value<CC,uint64_t>> operator[](size_t col) const {
		auto &self = static_cast<const Struct<CC,Table>&>(*this);
		//FIXME: loaded every time
		auto vr_nrows = self.template get_value<Table::member_nrows>();
		auto vr_data = self.template get_value<Table::member_data>();
		//coat::Value vr_col(self.cc, col, "col");
		//return vr_data + (vr_nrows *= vr_col);
		return vr_data + (vr_nrows *= col);
	}
};

}


using column_t = std::vector<uint64_t>;
using table_t = std::vector<column_t>;


static column_t calc_vector(const table_t &table, const char *operations){
	const size_t size = table[0].size();
	column_t result(size);
	auto t_start = std::chrono::high_resolution_clock::now();
	for(size_t i=0; i<size; ++i){
		const char *p = operations;
		int col = *p - '0';
		++p;
		uint64_t res = table[col][i];
		while(*p){
			switch(*p){
				case '+':
					col = p[1] - '0';
					res += table[col][i];
					break;
				case '-':
					col = p[1] - '0';
					res -= table[col][i];
					break;
				default:
					printf("unsupported operation: %c\n", *p);
					std::exit(-1);
			}
			p += 2;
		}
		result[i] = res;
	}
	auto t_end = std::chrono::high_resolution_clock::now();
	printf("vector: %.2f us\n", std::chrono::duration<double, std::micro>( t_end - t_start).count());
	return result;
}

static column_t calc_table(const Table &table, const char *operations){
	const size_t size = table.nrows;
	column_t result(size);
	auto t_start = std::chrono::high_resolution_clock::now();
	for(size_t i=0; i<size; ++i){
		const char *p = operations;
		int col = *p - '0';
		++p;
		uint64_t res = table[col][i];
		while(*p){
			switch(*p){
				case '+':
					col = p[1] - '0';
					res += table[col][i];
					break;
				case '-':
					col = p[1] - '0';
					res -= table[col][i];
					break;
				default:
					printf("unsupported operation: %c\n", *p);
					std::exit(-1);
			}
			p += 2;
		}
		result[i] = res;
	}
	auto t_end = std::chrono::high_resolution_clock::now();
	printf("table: %.2f us\n", std::chrono::duration<double, std::micro>( t_end - t_start).count());
	return result;
}


static column_t jit(const Table &table, const char *operations){
	const size_t size = table.nrows;
	column_t result(size);

	auto t_start = std::chrono::high_resolution_clock::now();

	coat::runtimeasmjit asmrt;
	using func_t = void (*)(const Table *table, uint64_t *result, size_t size);
	coat::Function<coat::runtimeasmjit,func_t> fn(&asmrt);
	{
		auto args = fn.getArguments("table", "result", "size");
		auto &vr_table = std::get<0>(args);
		auto &vr_result = std::get<1>(args);
		auto &vr_size = std::get<2>(args);

		coat::Value vr_index(fn, 0UL, "index");
		coat::loop_while(fn, vr_index < vr_size, [&]{
			const char *p = operations;
			int col = *p - '0';
			++p;
			coat::Value<asmjit::x86::Compiler,uint64_t> vr_res(fn, "res");
			vr_res = vr_table[col][vr_index];
			while(*p){
				switch(*p){
					case '+':
						col = p[1] - '0';
						vr_res += vr_table[col][vr_index];
						break;
					case '-':
						col = p[1] - '0';
						vr_res -= vr_table[col][vr_index];
						break;
					default:
						printf("unsupported operation: %c\n", *p);
						std::exit(-1);
				}
				p += 2;
			}
			vr_result[vr_index] = vr_res;
			++vr_index;
		});
	}
	// finalize function
	func_t fnptr = fn.finalize(&asmrt);

	auto t_compile = std::chrono::high_resolution_clock::now();
	// execute generated function
	fnptr(&table, result.data(), size);

	asmrt.rt.release(fnptr);

	auto t_end = std::chrono::high_resolution_clock::now();
	printf("jit1: %.2f us (compilation: %.2f; exec: %.2f)\n",
		std::chrono::duration<double, std::micro>( t_end - t_start).count(),
		std::chrono::duration<double, std::micro>( t_compile - t_start).count(),
		std::chrono::duration<double, std::micro>( t_end - t_compile).count()
	);

	return result;
}

static column_t jit2(const Table &table, const char *operations){
	const size_t size = table.nrows;
	column_t result(size);

	auto t_start = std::chrono::high_resolution_clock::now();

	coat::runtimeasmjit asmrt;
	using func_t = void (*)(const Table *table, uint64_t *result, size_t size);
	coat::Function<coat::runtimeasmjit,func_t> fn(&asmrt);
	{
		auto args = fn.getArguments("table", "result", "size");
		auto &vr_table = std::get<0>(args);
		auto &vr_result = std::get<1>(args);
		auto &vr_size = std::get<2>(args);

		auto vr_nrows = vr_table.get_value<Table::member_nrows>();
		auto vr_data = vr_table.get_value<Table::member_data>();
		coat::Value vr_index(fn, 0UL, "index");
		coat::loop_while(fn, vr_index < vr_size, [&]{
			const char *p = operations;
			int col = *p - '0';
			++p;
			coat::Value<asmjit::x86::Compiler,uint64_t> vr_res(fn, "res");
			//vr_res = vr_table[col][vr_index];
			auto vr_col = vr_data + (vr_nrows * col);
			vr_res = vr_col[vr_index];
			while(*p){
				switch(*p){
					case '+': {
						col = p[1] - '0';
						//vr_res += vr_table[col][vr_index];
						auto vr_col = vr_data + (vr_nrows * col);
						vr_res += vr_col[vr_index];
						break;
					}
					case '-': {
						col = p[1] - '0';
						//vr_res -= vr_table[col][vr_index];
						auto vr_col = vr_data + (vr_nrows * col);
						vr_res -= vr_col[vr_index];
						break;
					}
					default:
						printf("unsupported operation: %c\n", *p);
						std::exit(-1);
				}
				p += 2;
			}
			vr_result[vr_index] = vr_res;
			++vr_index;
		});
	}
	// finalize function
	func_t fnptr = fn.finalize(&asmrt);

	auto t_compile = std::chrono::high_resolution_clock::now();
	// execute generated function
	fnptr(&table, result.data(), size);

	asmrt.rt.release(fnptr);

	auto t_end = std::chrono::high_resolution_clock::now();
	printf("jit2: %.2f us (compilation: %.2f; exec: %.2f)\n",
		std::chrono::duration<double, std::micro>( t_end - t_start).count(),
		std::chrono::duration<double, std::micro>( t_compile - t_start).count(),
		std::chrono::duration<double, std::micro>( t_end - t_compile).count()
	);

	return result;
}

static column_t jit3(const Table &table, const char *operations){
	const size_t size = table.nrows;
	column_t result(size);

	auto t_start = std::chrono::high_resolution_clock::now();

	coat::runtimeasmjit asmrt;
	using func_t = void (*)(uint64_t *result, size_t size);
	coat::Function<coat::runtimeasmjit,func_t> fn(&asmrt);
	{
		auto args = fn.getArguments("result", "size");
		auto &vr_result = std::get<0>(args);
		auto &vr_size = std::get<1>(args);

		coat::Value vr_index(fn, 0UL, "index");
		coat::loop_while(fn, vr_index < vr_size, [&]{
			const char *p = operations;
			int col = *p - '0';
			++p;
			coat::Value<asmjit::x86::Compiler,uint64_t> vr_res(fn, "res");
			//vr_res = vr_table[col][vr_index];
			const uint64_t *column = table[col];
			auto vr_col = fn.makePointer(column, "col");
			vr_res = vr_col[vr_index];
			while(*p){
				switch(*p){
					case '+': {
						col = p[1] - '0';
						//vr_res += vr_table[col][vr_index];
						const uint64_t *column = table[col];
						auto vr_col = fn.makePointer(column, "col");
						vr_res += vr_col[vr_index];
						break;
					}
					case '-': {
						col = p[1] - '0';
						//vr_res -= vr_table[col][vr_index];
						const uint64_t *column = table[col];
						auto vr_col = fn.makePointer(column, "col");
						vr_res -= vr_col[vr_index];
						break;
					}
					default:
						printf("unsupported operation: %c\n", *p);
						std::exit(-1);
				}
				p += 2;
			}
			vr_result[vr_index] = vr_res;
			++vr_index;
		});
	}
	// finalize function
	func_t fnptr = fn.finalize(&asmrt);

	auto t_compile = std::chrono::high_resolution_clock::now();
	// execute generated function
	fnptr(result.data(), size);

	asmrt.rt.release(fnptr);

	auto t_end = std::chrono::high_resolution_clock::now();
	printf("jit3: %.2f us (compilation: %.2f; exec: %.2f)\n",
		std::chrono::duration<double, std::micro>( t_end - t_start).count(),
		std::chrono::duration<double, std::micro>( t_compile - t_start).count(),
		std::chrono::duration<double, std::micro>( t_end - t_compile).count()
	);

	return result;
}


static void write(const column_t &result, const char *name){
	FILE *fd = fopen(name, "w");
	for(uint64_t r : result){
		fprintf(fd, "%lu\n", r);
	}
	fclose(fd);
}

int main(int argc, char **argv){
	size_t ncol = atoi(argv[1]);
	size_t size = atoi(argv[2]);
	size_t repetitions = atoi(argv[4]);
	bool dump=false;

	std::mt19937 gen(42);
	{
		table_t table(ncol);
		for(size_t i=0;i<ncol; ++i){
			table[i].resize(size);
			std::iota(table[i].begin(), table[i].end(), 0);
			std::shuffle(table[i].begin(), table[i].end(), gen);
		}

		for(size_t r=0; r<repetitions; ++r){
			column_t result = calc_vector(table, argv[3]);
			if(dump) write(result, "calc_vector.dump");
		}
	}

	gen.seed(42);
	{
		Table table(ncol, size);
		for(size_t i=0;i<ncol; ++i){
			std::iota(table[i], table[i] + table.nrows, 0);
			std::shuffle(table[i], table[i] + table.nrows, gen);
		}

		for(size_t r=0; r<repetitions; ++r){
			column_t result = calc_table(table, argv[3]);
			if(dump) write(result, "calc_table.dump");
		}
		for(size_t r=0; r<repetitions; ++r){
			column_t result = jit(table, argv[3]);
			if(dump) write(result, "calc_jit.dump");
		}
		for(size_t r=0; r<repetitions; ++r){
			column_t result = jit2(table, argv[3]);
			if(dump) write(result, "calc_jit2.dump");
		}
		for(size_t r=0; r<repetitions; ++r){
			column_t result = jit3(table, argv[3]);
			if(dump) write(result, "calc_jit3.dump");
		}
	}

	return 0;
}