#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>

#define ROWS 11 // y
#define COLS 32 // x

const char * next_map = "";
volatile int escape_flag = 0;
volatile char game_state[ROWS][COLS];
volatile char persist_array[ROWS][COLS];
volatile int playerX = COLS / 2;
volatile int playerY = ROWS / 2;
volatile int collision = 1;
volatile int death = 0;
volatile int death_text_printed = 0;
int init = 1;
int max_boxes = 15;
volatile int box_count = 0;
volatile int menu_state = -1;
volatile int win_map = 0;

char keybinds[14] = {'w','a','s','d','r','q','\n','n','\\','e','1','2','3','f'};

typedef struct {
	int x;
	int y;
	int id;
	char state;
} box;

typedef struct {
	int dx;
	int dy;
	int dir;
} Move;

box** boxes;

char game_map[ROWS * (COLS + 1) + 1];
char persist_map[ROWS * (COLS + 1) + 1];

Move get_move(const char input) {
    const char* found_char = strchr(keybinds, input);
    if (found_char != NULL) {
	    const int index =  (int)(found_char - keybinds);
	    switch (index) {
	        case 0: return (Move){ 0, -1, 1 }; // Up
        	case 2: return (Move){ 0,  1, 3 }; // Down
        	case 1: return (Move){ -1, 0, 2 }; // Left
        	case 3: return (Move){ 1,  0, 4 }; // Right
        	default: return (Move){ 0,  0, 0 };
	    }
    } else {
	    return (Move){ 0,  0, 0 };
	}
}

void create_box(const int x, const int y) {
	box* new_box = (box*)malloc(sizeof(box));
	if (new_box == NULL) {
		perror("Failed to allocate memory for box creation");
		exit(EXIT_FAILURE);
	}
	if (box_count >= max_boxes) {
		perror("Failed to create box, max boxes hit");
		exit(EXIT_FAILURE);
	}

	new_box->x = x;
	new_box->y = y;
	new_box->id = box_count;

	boxes[box_count] = new_box;
	box_count++;
}

void clear_screen() {
	printf("\x1B[1;1H\x1B[2J");
}

char* get_user_input() {
	const unsigned int init_buffer_size = 256;
	size_t size = init_buffer_size;
	char* buffer = malloc(size);
	if (!buffer) {
		perror("Unable to allocate buffer");
		exit(EXIT_FAILURE);
	}

	size_t len = 0;
	int ch;

	while (!isblank((ch = getchar())) && ch != EOF && ch != '\n') {
		if (len + 1 >= size) {
			size *= 2;
			char* new_buffer = realloc(buffer, size);
			if (!new_buffer) {
				free(buffer);
				perror("Unable to reallocate buffer");
				exit(EXIT_FAILURE);
			}
			buffer = new_buffer;
		}
		buffer[len++] = ch;
	}
	buffer[len] = '\0';

	if (len == 0) {
		free(buffer);
		return "";
	}
	return buffer;
}

void render_game() {
	char buffer[ROWS * (COLS * 10) + 1];
	int index = 0;
	if (boxes != NULL) {
		for (int c = 0; c < box_count; c++) {
			if (boxes[c] != NULL) {
				const int boxx = boxes[c]->x;
				const int boxy = boxes[c]->y;
				if (boxx > -1 && boxx < COLS && boxy > -1 && boxy < ROWS) {
					game_state[boxes[c]->y][boxes[c]->x] = '%';
				}
			}
		}
	}

	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			switch(game_state[i][j]) {
			case '_':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[31m\x1B[21m");
				buffer[index++] = game_state[i][j];
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			case ' ':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[32m\x1B[102m");
				buffer[index++] = '#';
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			case 'P':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[38;5;93m");
				buffer[index++] = game_state[i][j];
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			case '@':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[92m");
				buffer[index++] = game_state[i][j];
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			case '%':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[93m");
				buffer[index++] = game_state[i][j];
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			case '=':
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[36m");
				buffer[index++] = game_state[i][j];
				index += snprintf(&buffer[index], sizeof(buffer) - index, "\x1B[0m");
				break;
			default:
				buffer[index++] = game_state[i][j];
				break;
			}
		}
		buffer[index++] = '\n';
	}
	buffer[index] = '\0';

	clear_screen();
	printf("%s", buffer);
	printf("\nWASD - Move    R - Restart    Q - Quit to menu    %s\n",collision ? "" : "NOCLIP");
}

int compare_2d_arrays(char arr1[ROWS][COLS], char arr2[ROWS][COLS]) {
	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			if (arr1[i][j] != arr2[i][j]) {
				return 0;
			}
		}
	}
	return 1;
}

void copy_2d_array(char dest[ROWS][COLS], char src[ROWS][COLS]) {
	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			dest[i][j] = src[i][j];
		}
	}
}

void remove_box(const int x, const int y) {
	for (int i = 0; i < box_count; i++) {
		if (boxes[i]->x == x && boxes[i]->y == y) {
			free(boxes[i]);
			if (i != box_count - 1) {
				boxes[i] = boxes[box_count - 1];
			}
			box_count--;
			boxes[box_count] = NULL;
			return;
		}
	}
}

void reset_boxes() {
	if (boxes != NULL) {
		for (int i = 0; i < box_count; i++) {
			free(boxes[i]);
		}
		free(boxes);
		boxes = NULL;
	}

	boxes = (box**)malloc(max_boxes * sizeof(box*));
	if (boxes == NULL) {
		perror("Failed to reallocate memory for boxes");
		exit(EXIT_FAILURE);
	}

	box_count = 0;
}

void load_initial_game_state() {
	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			game_state[i][j] = '.';
			persist_array[i][j] = '.';
		}
	}

	reset_boxes();

	int x = 0, y = 0;
	for (int i = 0; game_map[i] != '\0'; i++) {
		if (game_map[i] == '\n') {
			y++;
			x = 0;
			continue;
		}
		if (y >= ROWS) break;
		if (x < COLS) {
			game_state[y][x] = game_map[i];

			if (game_map[i] == '@') {
				playerX = x;
				playerY = y;
			}
			if (game_map[i] == '%') {
				create_box(x, y);
			}
		}
		x++;
	}
	x = 0;
	y = 0;
	for (int a = 0; persist_map[a] != '\0'; a++) {
		if (persist_map[a] == '\n') {
			y++;
			x = 0;
			continue;
		}
		if (y >= ROWS) break;
		if (x < COLS) {
			persist_array[y][x] = persist_map[a];
		}
		x++;
	}
	for (int c = 0; c < ROWS; c++) {
		for (int v = 0; v < COLS; v++) {
			if (persist_array[c][v] != '.') {
				game_state[c][v] = persist_array[c][v];
			}
		}
	}
}

void* update_game_state(void* arg) {
	const int init = *(int*)arg;

	if (init) {
		load_initial_game_state();
		render_game();
	} else {
		char prev_game_state[ROWS][COLS];
		copy_2d_array(prev_game_state, (char (*)[COLS])game_state);

		if (collision) {
			playerX = (playerX < 0) ? 0 : (playerX > COLS - 1) ? COLS - 1 : playerX;
			playerY = (playerY < 0) ? 0 : (playerY > ROWS - 1) ? ROWS - 1 : playerY;
		} else {
			playerX = (playerX + COLS) % COLS;
			playerY = (playerY + ROWS) % ROWS;
		}
		for (int i = 0; i < ROWS; i++) {
			for (int j = 0; j < COLS; j++) {
				if (persist_array[i][j] != '.') {
					game_state[i][j] = persist_array[i][j];
				}
				if (game_state[i][j] == '_' || game_state[i][j] == ' ' || game_state[i][j] == 'P') {
					continue;
				}
				if (playerX == j && playerY == i) {
					game_state[i][j] = '@';
					continue;
				}
				if (game_state[i][j] == '#' || game_state[i][j] == '=') {
					continue;
				}
				game_state[i][j] = '.';
			}
		}

		if (!compare_2d_arrays(prev_game_state, (char (*)[COLS])game_state)) {
			render_game();
		}
	}
	return NULL;
}

void set_nonblocking(const int state, const int nonblock) {
	struct termios ttystate;

	tcgetattr(STDIN_FILENO, &ttystate);

	if (state) {
		ttystate.c_lflag &= ~(ICANON | ECHO);
		ttystate.c_cc[VMIN] = 1;
	} else {
		ttystate.c_lflag |= ICANON | ECHO;
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (nonblock) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}
	fcntl(STDIN_FILENO, F_SETFL, flags);
}

box* find_box(int x, int y) {
	for (int i = 0; i < box_count; i++) {
		if (boxes[i]->x == x && boxes[i]->y == y) {
			return boxes[i];
		}
	}
	return NULL;
}

int box_check(const int x, const int y, const int direction) {
	if (!collision) return 1;

	box* b = find_box(x, y);
	if (!b || direction-1 < 0 || direction > 4) {
	    return 1;
	}

	const Move move = get_move(keybinds[direction-1]);

	const int new_x = b->x + move.dx;
	const int new_y = b->y + move.dy;

	if (new_x >= 0 && new_x < COLS && new_y >= 0 && new_y < ROWS) {
		const char cell = game_state[new_y][new_x];
		if (cell == '.' || cell == '_' || cell == ' ') {
			b->x = new_x;
			b->y = new_y;
		} else if (cell == '=') {
			return 0;
		}
	}
	char cell = game_state[b->y][b->x];
	if (cell == '_' || cell == ' ') {
		game_state[b->y][b->x] = (cell == '_') ? '.' : cell;
		remove_box(b->x, b->y);
	}

	return 1;
}

int check_collision(int x, int y) {
	for (int i = 0; i < box_count; i++) {
		if (boxes[i]->x == x && boxes[i]->y == y) {
			if (box_check(boxes[i]->x, boxes[i]->y, 0) != 0 && collision) {
				return 0;
			}
		}
	}
	if (game_state[y][x] == '#' && collision) {
		return 0;
	}
	return 1;
}

void save_editor() {
	int index = 0;
	char buffer[ROWS * (COLS + 1) + 1];
	for (int r = 0; r < ROWS; r++) {
		for (int c = 0; c < COLS; c++) {
			buffer[index] = game_state[r][c];
			index++;
		}
		buffer[index] = '\n';
		index++;
	}
	buffer[index] = '\0';
	strcpy(game_map, buffer);

	int Pindex = 0;
	char Pbuffer[ROWS * (COLS + 1) + 1];
	for (int Pr = 0; Pr < ROWS; Pr++) {
		for (int Pc = 0; Pc < COLS; Pc++) {
			Pbuffer[Pindex] = persist_array[Pr][Pc];
			Pindex++;
		}
		Pbuffer[Pindex] = '\n';
		Pindex++;
	}
	Pbuffer[Pindex] = '\0';
	strcpy(persist_map, Pbuffer);
}

int load_map(const char *filepath) {
	char filename[strlen(filepath) + 5];
	snprintf(filename, sizeof(filename), "%s.map", filepath);
	FILE* map_file = fopen(filename, "r");
	if (!map_file) {
		perror("fopen");
	}
	if (map_file == NULL) {
		return 3;
	}

	fseek(map_file, 0, SEEK_END);
	const long file_size = ftell(map_file);
	fseek(map_file, 0, SEEK_SET);

	char* buffer = malloc(file_size+1);
	if (buffer == NULL) {
		perror("Failed to allocate memory");
		fclose(map_file);
		exit(EXIT_FAILURE);
	}
	const size_t bytesRead = fread(buffer, 1, file_size, map_file);
	if (bytesRead != file_size) {
		free(buffer);
		return 2;
	}
	buffer[file_size] = '\0';
	fclose(map_file);

	if(strstr(buffer, "END\n") == NULL || strstr(buffer, "map:\n") == NULL) {
		free(buffer);
		return 2;
	}

	char* section = strtok(buffer, "END");
	if (strlen(section) - 5 != ROWS * (COLS + 1)) {
		free(buffer);
		return 2;
	}
	while (section) {
		if (strstr(section, "map:") != NULL) {
			section += 5;
			int row = 0;
			int col = 0;
			int i = 0;

			while (section[i] != '\0') {
				if (section[i] == '\n') {
					row++;
					col = 0;
				} else {
					if (col < COLS) {
						game_state[row][col] = section[i];
						col++;
					}
				}

				if (row >= ROWS) {
					break;
				}

				i++;
			}
		} else if (strstr(section, "persist:") != NULL) {
			section += 10;
			int row = 0;
			int col = 0;
			int i = 0;

			while (section[i] != '\0') {
				if (section[i] == '\n') {
					row++;
					col = 0;
				} else {
					if (col < COLS) {
						persist_array[row][col] = section[i];
						col++;
					}
				}

				if (row >= ROWS) {
					break;
				}

				i++;
			}
		} else if (strstr(section, "next:") != NULL) {
			section += 6;
			next_map = section;
		}
		section = strtok(NULL, "END");
	}

	free(buffer);
	save_editor();
	return 0;
}

void render_editor(const int state) {
	char buffer[ROWS * (COLS * 10) + 1];
	int index = 0;

	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			char ch;
			if (state) {
				ch = game_state[i][j];
			} else {
				ch = persist_array[i][j];
			}
			if (index + 1 < sizeof(buffer)) {
				buffer[index++] = ch;
			} else {
				buffer[sizeof(buffer) - 1] = '\0';
				goto buffer_full;
			}
			if (playerY == i && playerX == j) {
				buffer[index - 1] = '!';
			}
		}
		if (index + 1 < sizeof(buffer)) {
			buffer[index++] = '\n';
		} else {
			buffer[sizeof(buffer) - 1] = '\0';
			goto buffer_full;
		}
	}

buffer_full:
	buffer[index] = '\0';
	clear_screen();
	printf("%s", buffer);
	printf("\nWASD - Move cursor    E - Switch map mode    Current map: %s\nQ - Quit editor    1 - Save    2 - Export map    F - Set next map: %s.map", state ? "Regular" : "Persist",next_map);
}

void respawn(pthread_mutex_t game_state_mutex) {
	pthread_mutex_lock(&game_state_mutex);
	death = 0;
	win_map = 0;
	death_text_printed = 0;
	init = 1;
	playerX = COLS / 2;
	playerY = ROWS / 2;
	update_game_state(&init);
	init = 0;
	pthread_mutex_unlock(&game_state_mutex);
}

int handle_gameplay() {
	int row = 0,col = 0;
	for (int c = 0; persist_map[c] != '\0'; c++) {
		if (persist_map[c] == '\n') {
			row++;
			col = 0;
		} else {
			persist_array[row][col] = persist_map[c];
			col++;
		}
	}
	printf("\x1B[?25l");
	init = 1;
	update_game_state(&init);

	pthread_t gamethread;

	init = 0;
	pthread_create(&gamethread, NULL, update_game_state, &init);
	pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;

	set_nonblocking(1, 1);
	while (!escape_flag) {
		if (win_map) {
			if (!death_text_printed) {
				clear_screen();
				const char *death_text = "\x1B[38;5;42m /$$     /$$                        /$$      /$$ /$$          \n|  $$   /$$/                       | $$  /$ | $$|__/          \n \\  $$ /$$//$$$$$$  /$$   /$$      | $$ /$$$| $$ /$$ /$$$$$$$ \n  \\  $$$$//$$__  $$| $$  | $$      | $$/$$ $$ $$| $$| $$__  $$\n   \\  $$/| $$  \\ $$| $$  | $$      | $$$$_  $$$$| $$| $$  \\ $$\n    | $$ | $$  | $$| $$  | $$      | $$$/ \\  $$$| $$| $$  | $$\n    | $$ |  $$$$$$/|  $$$$$$/      | $$/   \\  $$| $$| $$  | $$\n    |__/  \\______/  \\______/       |__/     \\__/|__/|__/  |__/\n\x1B[0m";
				printf("%s",death_text);
				if (strcmp(next_map,"") != 0) {
					printf("-%c to go to the next map   ",keybinds[7]);
				}
				printf("-%c to respawn   -%c to quit to menu",keybinds[4],keybinds[5]);
				death_text_printed = 1;
				set_nonblocking(1, 0);
			} else {
				const char ch = tolower(getchar());
				if (ch ==keybinds[4]) {
				    respawn(game_state_mutex);
				}
				if (ch ==keybinds[7]) {
				    if (strcmp(next_map,"") != 0) {
						pthread_mutex_lock(&game_state_mutex);
						win_map = 0;
						death_text_printed = 0;
						set_nonblocking(1, 1);
						pthread_mutex_unlock(&game_state_mutex);
						return 5;
					}
				}
				if (ch ==keybinds[5]) {
				    death = 0;
					death_text_printed = 0;
					escape_flag = 1;
					clear_screen();
				}
			}
		} else {
			if (death) {
				if (!death_text_printed) {
					clear_screen();
					printf("\x1B[41m /$$     /$$                        /$$$$$$$  /$$                 /$$\n|  $$   /$$/                       | $$__  $$|__/                | $$\n \\  $$ /$$//$$$$$$  /$$   /$$      | $$  \\ $$ /$$  /$$$$$$   /$$$$$$$\n  \\  $$$$//$$__  $$| $$  | $$      | $$  | $$| $$ /$$__  $$ /$$__  $$\n   \\  $$/| $$  \\ $$| $$  | $$      | $$  | $$| $$| $$$$$$$$| $$  | $$\n    | $$ | $$  | $$| $$  | $$      | $$  | $$| $$| $$_____/| $$  | $$\n    | $$ |  $$$$$$/|  $$$$$$/      | $$$$$$$/| $$|  $$$$$$$|  $$$$$$$\n    |__/  \\______/  \\______/       |_______/ |__/ \\_______/ \\_______/\n\x1B[0m-%c to respawn   -%c to quit to menu",keybinds[4],keybinds[5]);
					death_text_printed = 1;
					set_nonblocking(1, 0);
				} else {
					const char ch = tolower(getchar());
					if (ch ==keybinds[4]) {
				        respawn(game_state_mutex);
				    }
				    if (ch ==keybinds[5]) {
    				    death = 0;
    					death_text_printed = 0;
    					escape_flag = 1;
    					clear_screen();
				    }
				}
			} else {
				const char ch = tolower(getchar());
				if (ch != EOF) {
					const char* found_char = strchr(keybinds, ch);
				    if (found_char != NULL) {
					    const int index =  (int)(found_char - keybinds);
				    	if (index <= 3 && index >= 0 ) {
				    		const Move move = get_move(ch);
				    		if (box_check(playerX + move.dx, playerY + move.dy, move.dir ) == 1 && check_collision(playerX + move.dx, playerY + move.dy)) {
				    			playerX += move.dx;
				    			playerY += move.dy;
				    		}
				    	}
				    	if (ch == keybinds[5]) {
				    		escape_flag = 1;
				    		clear_screen();
				    	}
				    	if (ch == keybinds[4]) {
				    		respawn(game_state_mutex);
				    	}
				    	if (ch == keybinds[8]) {
				    		collision = !collision;  //noclip toggle
				    	}
				    }
					if (!escape_flag) {
						update_game_state(&init);
					}
				}
				if (!escape_flag) {
					if (game_state[playerY][playerX] == 'P' && collision) {
						win_map = 1;
					}
					if ((game_state[playerY][playerX] == '_' || game_state[playerY][playerX] == ' ') && collision) {
						death = 1;
					}
				}
			}
		}
	}
	set_nonblocking(0, 0);

	pthread_join(gamethread, NULL);
	pthread_mutex_destroy(&game_state_mutex);

	reset_boxes();
	return 1;
}

int handle_editor() {
	int x = 0, y = 0;
	for (int i = 0; game_map[i] != '\0'; i++) {
		if (game_map[i] == '\n') {
			y++;
			x = 0;
			continue;
		}
		if (y >= ROWS) break;
		if (x < COLS) {
			game_state[y][x] = game_map[i];
		}
		x++;
	}
	int row = 0,col = 0;
	for (int c = 0; persist_map[c] != '\0'; c++) {
		if (persist_map[c] == '\n') {
			row++;
			col = 0;
		} else {
			persist_array[row][col] = persist_map[c];
			col++;
		}
	}
	int map_mode = 1;        // Boolean, persist map editing or general map
	printf("\x1B[?25l");
	set_nonblocking(1, 1);
	render_editor(map_mode);

	while (!escape_flag) {
		const char ch = tolower(getchar());
		if (ch != EOF) {
			const char* found_char = strchr(keybinds, ch);
			if (found_char != NULL) {
				const int index =  (int)(found_char - keybinds);
				if (index <= 3 && index >= 0 ) {
		    		const Move move = get_move(ch);
	    			playerX += move.dx;
	    			playerY += move.dy;
	    			playerX = (playerX + COLS) % COLS;
		            playerY = (playerY + ROWS) % ROWS;
		    	}

				if (ch == keybinds[9]) {
					map_mode = !map_mode;
				}
				if (ch == keybinds[5]) {
					return 1;
				}
				if (ch == keybinds[10]) {
					save_editor();
					printf("\n\nMap Saved.");
					continue;
				}
				if (ch == keybinds[11]) {
					save_editor();
					printf("\x1B[?25h");
					printf("\n\nMap name: ");
					set_nonblocking(0,0);
					char* input = get_user_input();
					set_nonblocking(1,0);
					if (input != NULL) {
    					if (strlen(input) > 0) {
    					    render_editor(map_mode);
    						printf("\x1B[?25l");
    						FILE *map_file = fopen(strcat(input,".map"),"w");
    						fprintf(map_file,"map:\n%sEND\npersist:\n%sEND\nnext:%sEND\n", game_map, persist_map, next_map);
    						fclose(map_file);
    						printf("\n\nMap Exported. (%s)",input);
    					}else {
    					    render_editor(map_mode);
    						printf("\x1B[?25l");
    						printf("\n\nMap name must be more than one character.");
    					}
					}else {
					    render_editor(map_mode);
						printf("\x1B[?25l");
						printf("\n\nMap name must be more than one character.");
					}
						
    				free(input);
    				continue;
					
				}
				if (ch == keybinds[13]) {
					printf("\x1B[?25h");
					printf("\n\nMap name: ");
					set_nonblocking(0,0);
					const char* next_input = get_user_input();
					set_nonblocking(1,0);
					if(*next_input && strcmp(next_input,"") != 0) {
						printf("\x1B[?25l");
						next_map = next_input;
						render_editor(map_mode);
					}
					continue;
				}
			}else {
				if (isprint(ch)) {
					if (map_mode) {
						game_state[playerY][playerX] = ch;
					} else {
						persist_array[playerY][playerX] = ch;
					}
				}
			}
			render_editor(map_mode);
		}
	}
	return 4;
}

int main(int argc, char *argv[]) {
	setlocale(LC_ALL, "en_US.UTF-8");
	for (int i = 0; i < ROWS; i++) {
		for (int j = 0; j < COLS; j++) {
			game_state[i][j] = '.';
			persist_array[i][j] = '.';
		}
	}
main_menu:
	;
	escape_flag = 0;
	clear_screen();
	printf("\x1B[?25l");
	printf("=========Main Menu=========\n       1) Play\n       2) Editor\n       3) Quit");
input:
	;
	set_nonblocking(1,0);
	const char ch = tolower(getchar());
	if (ch != ' ' && ch != '\n' && ch != '\t' && ch != EOF) {
		if (isdigit(ch)) {
			const char str[2] = {ch,'\0'};
			menu_state = atoi(str);
		}
		else {
			goto main_menu;
		}
		switch(menu_state) {
		case 1:
play_menu_start:
			;
			char* menu_text = "=========Play Menu=========\n       1) Play Loaded Map\n       2) Load Map\n       3) Back";
			escape_flag = 0;
			clear_screen();
			printf("%s",menu_text);
play_menu:
			;
			set_nonblocking(1,0);
			printf("\x1B[?25l");
			char ch1 = tolower(getchar());
			if (ch1 != ' ' && ch1 != '\n' && ch1 != '\t' && ch1 != EOF) {
				unsigned short int play_state = 0;
				if (isdigit(ch1)) {
					const char str[2] = {ch1,'\0'};
					play_state = atoi(str);
				}
				else {
					goto play_menu;
				}
				clear_screen();
				printf("%s",menu_text);
play_switch:
				;
				switch(play_state) {
				case 1:
					switch (handle_gameplay()) {
					case 1:
						goto main_menu;
					case 5:
						switch (load_map(next_map)) {
						case 3:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nMap not found.");
							break;
						case 2:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nMap is malformed or corrupted.");
							break;
						case 0:
							play_state = 1;
							goto play_switch;
						default:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nSomething went horribly wrong.");
							break;
						}
					default:
						break;
					}
					break;
				case 2:
					printf("\x1B[?25h");
					printf("\n\nMap name: ");
					set_nonblocking(0,0);
					const char* input = get_user_input();
					set_nonblocking(1,0);
					if(*input && strcmp(input,"") != 0) {
						clear_screen();
						printf("\x1B[?25l");
						printf("%s",menu_text);
						switch (load_map(input)) {
						case 3:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nMap not found.");
							break;
						case 2:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nMap is malformed or corrupted.");
							break;
						case 0:
							play_state = 1;
							goto play_switch;
						default:
							clear_screen();
							printf("\x1B[?25l");
							printf("%s",menu_text);
							printf("\n\nSomething went horribly wrong.");
							break;
						}
					} else {
						clear_screen();
						printf("%s",menu_text);
						printf("\n\nCancelled.");
						goto play_menu;
					}
					break;
				case 3:
					goto main_menu;
				default:
					goto play_menu;
				}
			} else {
				goto play_menu_start;
			}
			goto play_menu;
		case 2:
			if (handle_editor() == 1) {
				goto main_menu;
			}
			break;
		case 4:
			reset_boxes();
			if (handle_gameplay() == 1) {
				goto main_menu;
			}
			break;
		case 3:
			break;
		default:
			goto main_menu;
		}
	} else {
		goto input;
	}
	clear_screen();
	printf("\x1B[?25h");
	return 0;
}
