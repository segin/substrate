int vis_obj __attribute__((visibility("hidden"), section(".attr.data"), aligned(32), weak, used)) = 5;

int main(void) {
	return vis_obj == 5 ? 0 : 1;
}
