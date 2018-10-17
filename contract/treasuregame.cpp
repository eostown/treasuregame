#include <eosiolib/eosio.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/currency.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/multi_index.hpp>

using namespace eosio;
using namespace std;

/*
using eosio::asset;
using eosio::indexed_by;
using eosio::const_mem_fun;
using eosio::permission_level;
using eosio::action;
using eosio::unpack_action_data;
*/

#define EOS_SYMBOL S(4, EOS)

//@abi table state i64
struct state {
    uint64_t id;
    uint64_t value;

    uint64_t primary_key()const { return id; }

    EOSLIB_SERIALIZE( state, (id)(value) )
};

struct player {
    account_name name;
    uint64_t ticketNo;

    EOSLIB_SERIALIZE( player, (name)(ticketNo) )
};

//@abi table game i64
struct game {
    uint64_t id;
    uint64_t price;
    uint64_t total_amount;
    uint64_t total_count;
    account_name owner;
    account_name starter;
    account_name drawer;
    uint16_t fee_percent;
    uint64_t start_fee;
    uint64_t draw_fee;
    time_point_sec created_at;
    uint64_t current_count;
    uint8_t status;
    player winner;

    string game_id_str() const {
        char a[30];
        sprintf(a, "%lld", id);
        return string(a);
    }

    uint64_t primary_key()const { return id; }

    EOSLIB_SERIALIZE( game, (id)(price)(total_amount)(total_count)(owner)(starter)(drawer)(fee_percent)(start_fee)(draw_fee)(created_at)(current_count)(status)(winner) )
};

//@abi table gameplayer i64
struct game_player {
    uint64_t id;
    uint64_t game_id;
    player player;
    time_point_sec created_at;

    uint64_t primary_key()const { return id; }

    EOSLIB_SERIALIZE( game_player, (id)(game_id)(player)(created_at) )
};

class treasuregame : public eosio::contract {

    public:
        const uint8_t OPEN = 0;
        const uint8_t CLOSE = 1;
        const uint8_t STOPPED = 2;

        const uint64_t ADMIN_ACCOUNT_NAME = 0;
        const uint64_t CURRENT_GAME_ID = 1;
        const uint64_t TOTAL_AMOUNT = 2;
        const uint64_t TOTAL_COUNT = 3;
        const uint64_t PLATFOMR_FEE_PERCENT = 4;
        const uint64_t START_EOS_AMOUNT = 5;
        const uint64_t DRAW_EOS_AMOUNT = 6;
        const uint64_t PLATFORM_FEE_ACCOUNT = 7;
        const uint64_t LAST_PLAYER = 8;
        const uint64_t LAST_PLAY_TIMESTAMP = 9;
        const uint64_t GAME_TIME_PERIOD = 10;

        const uint32_t MAX_GAME_COUNT = 1000000;
        const symbol_type CURRENCY_SYMBOL = EOS_SYMBOL;

        treasuregame(account_name self)
        :eosio::contract(self),
         states(_self, _self),
         games(_self, _self),
         gameplayers(_self, _self)
        {}

        struct setstate_args {
            uint64_t id; 
            uint64_t value;
        };

        auto get_current_game(bool is_open) {
            auto state_itr = states.find( CURRENT_GAME_ID );
            eosio_assert(state_itr != states.end(), "can't find current game id");
            auto cur_game_itr = games.find(state_itr->value);
            eosio_assert(cur_game_itr != games.end(), "can't find current game");
            
            if (is_open) {
                eosio_assert(cur_game_itr->status == OPEN, "current game is not open");
            } else {
                eosio_assert(cur_game_itr == games.end() || cur_game_itr->status != OPEN, "current game is open");
            }
            
            return cur_game_itr;
        }

        //@abi action
        void start(const account_name starter) {
            require_auth(starter);

            uint64_t game_amount = get_state(TOTAL_AMOUNT);
            eosio_assert( game_amount > 0, "game amount should be greater than 0" );
            uint64_t game_count = get_state(TOTAL_COUNT);
            eosio_assert( game_count > 0 && game_count < MAX_GAME_COUNT, "game count should be (0, 1000000)" );
            uint64_t game_price = game_amount / game_count;
            eosio_assert( game_count * game_price == game_amount, "game amount should be exact multiple of game price" );
            uint64_t fee_percent = get_state(PLATFOMR_FEE_PERCENT);
            eosio_assert( fee_percent > 0 && fee_percent < 100, "fee perchent should be (0, 100)" );
            uint64_t start_eos_fee = get_state(START_EOS_AMOUNT);
            uint64_t draw_eos_fee = get_state(DRAW_EOS_AMOUNT);

            auto cur_game_itr = get_current_game(false);

            auto new_game_itr = games.emplace(starter, [&](auto& game){
                game.id = games.available_primary_key();
                game.price = game_price;
                game.total_amount = game_amount;
                game.total_count = game_count;
                game.fee_percent = fee_percent;
                game.start_fee = start_eos_fee;
                game.draw_fee = draw_eos_fee;
                game.owner = _self;
                game.starter = starter;
                game.created_at = time_point_sec(now());
                game.current_count = 0;
                game.status = OPEN;
            });

            updatestate(CURRENT_GAME_ID, new_game_itr->id);
            updatestate(LAST_PLAYER, 0);
            updatestate(LAST_PLAY_TIMESTAMP, 0);
        }

        //@abi action
        void setadmin(const account_name admin) {
            require_auth(_self);

            auto itr = states.find(ADMIN_ACCOUNT_NAME);
            if( itr == states.end() ) {
                states.emplace(_self, [&](auto& state){
                    state.id = ADMIN_ACCOUNT_NAME;
                    state.value = admin;
                });
            } else {
                states.modify(itr, 0, [&]( auto& state ) {
                    state.value = admin;
                });
            }
        }

        //@abi action
        void setstate(const setstate_args& args) {
            account_name admin = get_state(ADMIN_ACCOUNT_NAME);
            if (admin == 0) {
                admin = _self;
            }
            require_auth(admin);

            auto itr = states.find(args.id);
            if (itr == states.end()) {
                states.emplace(admin, [&](auto& state){
                    state.id = args.id;
                    state.value = args.value;
                });
            } else {
                states.modify(itr, 0, [&]( auto& state ) {
                    state.value = args.value;
                });
            }
        }

        void updatestate(uint64_t id, uint64_t value) {
            auto itr = states.find(id);
            states.modify(itr, 0, [&]( auto& state ) {
                state.value = value;
            });
        }

        uint64_t get_state(uint64_t id) {
            uint64_t value = 0;
            auto itr = states.find(id);
            if (itr != states.end()) {
                value = itr->value;
            }
            return value;
        }

        bool is_stop() {
            uint64_t game_time_period = get_state(GAME_TIME_PERIOD);
            uint64_t last_play_timestamp = get_state(LAST_PLAY_TIMESTAMP);
            if (last_play_timestamp <= 0)
                return false;
            uint64_t current_time_period = time_point_sec(now()).sec_since_epoch() - last_play_timestamp;
            return current_time_period > game_time_period;
        }

        void transfer(const currency::transfer& args) {
            eosio_assert(args.quantity.symbol == CURRENCY_SYMBOL, "Must be CORE_SYMBOL");
            eosio_assert(args.quantity.is_valid(), "invalid quantity");
            eosio_assert(args.quantity.amount > 0, "must deposit positive quantity");

            if (args.from == _self || args.to != _self) {
                //outgoing transfer
                return;
            }

            auto cur_game_itr = get_current_game(true);

            uint64_t buy_count = args.quantity.amount / cur_game_itr->price;
            eosio_assert(buy_count * cur_game_itr->price == args.quantity.amount, "buy amount should be exact multiple of game price");
            eosio_assert(cur_game_itr->current_count + buy_count <= cur_game_itr->total_count, "buy amount exceeds remaining amount");

            eosio_assert(!is_stop(), "current game stopped");

            uint64_t new_count = cur_game_itr->current_count + 1;
            uint64_t end_count = cur_game_itr->current_count + buy_count;
            time_point_sec now_time = time_point_sec(now());
            while (new_count <= end_count) {
                gameplayers.emplace(_self, [&](auto& game_player){
                    game_player.id = cur_game_itr->id * MAX_GAME_COUNT + new_count;
                    game_player.game_id = cur_game_itr->id;
                    game_player.player.name = args.from;
                    game_player.player.ticketNo = new_count;
                    game_player.created_at = now_time;
                });
                new_count++;
            }

            games.modify(cur_game_itr, 0, [&]( auto& game ) {
                game.current_count = end_count;
            });

            updatestate(LAST_PLAYER, args.from);
            updatestate(LAST_PLAY_TIMESTAMP, now_time.sec_since_epoch());
        }

        void deleteplayer(uint64_t game_id, uint64_t game_count) {
            auto iter = gameplayers.lower_bound(game_id * MAX_GAME_COUNT + 1);
            eosio_assert(iter != gameplayers.end(), "no game player");
            std::vector<uint64_t> ids;

            for (uint64_t i = 0; iter != gameplayers.end() && i < game_count; ++iter, i++) {
                ids.push_back(iter->id);
            }

            for (uint64_t id : ids) {
                gameplayers.erase(gameplayers.find(id));
            }
        }

        void send_fee(const game& cur_game, uint64_t game_amount, account_name drawer, account_name winner) {
            uint64_t fee_percent = cur_game.fee_percent;
            uint64_t start_eos_fee = cur_game.start_fee;
            uint64_t draw_eos_fee = cur_game.draw_fee;
            uint64_t platform_fee_account = get_state(PLATFORM_FEE_ACCOUNT);
            uint64_t game_id = cur_game.id;
            string game_id_str = cur_game.game_id_str();

            uint64_t platform_fee = game_amount * fee_percent / 100;
            action(
                permission_level{ _self, N(active) },
                N(eosio.token), N(transfer),
                std::make_tuple(_self, platform_fee_account, asset(platform_fee, CURRENCY_SYMBOL), string("game " + game_id_str + " platform fee"))
            ).send();

            action(
                permission_level{ _self, N(active) },
                N(eosio.token), N(transfer),
                std::make_tuple(_self, cur_game.starter, asset(start_eos_fee, CURRENCY_SYMBOL), string("game " + game_id_str + " start fee"))
            ).send();

            action(
                permission_level{ _self, N(active) },
                N(eosio.token), N(transfer),
                std::make_tuple(_self, drawer, asset(draw_eos_fee, CURRENCY_SYMBOL), string("game " + game_id_str + " draw fee"))
            ).send();

            uint64_t bonus = game_amount - platform_fee - start_eos_fee - draw_eos_fee;
            eosio_assert(bonus > 0, "bonus is negative");
            action(
                permission_level{ _self, N(active) },
                N(eosio.token), N(transfer),
                std::make_tuple(_self, winner, asset(bonus, CURRENCY_SYMBOL), string("game " + game_id_str + " bonus"))
            ).send();
        }

        //@abi action
        void stop(const account_name stopper) {
            require_auth(stopper);

            auto cur_game_itr = get_current_game(true);
            eosio_assert(cur_game_itr->total_count > cur_game_itr->current_count, "current game is full");
            eosio_assert(is_stop(), "current game is not stopped");

            account_name last_player = get_state(LAST_PLAYER);

            games.modify(cur_game_itr, 0, [&]( auto& game ) {
                game.drawer = stopper;
                game.winner.name = last_player;
                game.status = STOPPED;
                game.winner.ticketNo = cur_game_itr->current_count;
            });

            send_fee(*cur_game_itr, cur_game_itr->price * cur_game_itr->current_count, stopper, last_player);
            deleteplayer(cur_game_itr->id, cur_game_itr->total_count);
        }

        //@abi action
        void draw(const account_name drawer) {
            require_auth(drawer);

            auto cur_game_itr = get_current_game(true);
            eosio_assert(cur_game_itr->total_count == cur_game_itr->current_count, "current game is not full");
            
            uint64_t game_id = cur_game_itr-> id;
            uint64_t win_number = gen_random(game_id, cur_game_itr->created_at.sec_since_epoch(), cur_game_itr->total_count);
            auto iter = gameplayers.find(game_id * MAX_GAME_COUNT + win_number);
            eosio_assert(iter != gameplayers.end() && iter->player.name > 0, "can't find winner");
            eosio_assert(win_number == iter->player.ticketNo, "winner ticker is wrong");
            account_name winner = iter->player.name;

            games.modify(cur_game_itr, 0, [&]( auto& game ) {
                game.drawer = drawer;
                game.winner.name = winner;
                game.winner.ticketNo = win_number;
                game.status = CLOSE;
            });

            send_fee(*cur_game_itr, cur_game_itr->total_amount, drawer, winner);
            deleteplayer(game_id, cur_game_itr->total_count);
        }

        uint64_t gen_random(uint64_t game_id, uint32_t created_at, uint64_t max) {
            checksum256 result;
            auto mixedBlock = tapos_block_prefix() + tapos_block_num() + game_id + created_at + current_time();

            const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
            sha256((char *)mixedChar, sizeof(mixedChar), &result);
            const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&result);
            return (p64[1] % max) + 1;
        }

    private:
        typedef eosio::multi_index< N(state), state> state_index;
        typedef eosio::multi_index< N(game), game> game_index;
        typedef eosio::multi_index< N(gameplayer), game_player
        > game_player_index;
        
        state_index states;
        game_index games;
        game_player_index gameplayers;

};

extern "C"
{
    [[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        treasuregame _game(receiver);

        switch (action)
        {
        case N(transfer):
            if (code == N(eosio.token))
            {
                _game.transfer(unpack_action_data<currency::transfer>());
            }
            break;
        case N(start):
            _game.start(unpack_action_data<account_name>());
            break;
        case N(setadmin):
            _game.setadmin(unpack_action_data<account_name>());
            break;
        case N(setstate):
            _game.setstate(unpack_action_data<treasuregame::setstate_args>());
            break;
        case N(draw):
            _game.draw(unpack_action_data<account_name>());
            break;
        case N(stop):
            _game.stop(unpack_action_data<account_name>());
            break;
        default:
            break;
        }
        eosio_exit(0);
    }
}
