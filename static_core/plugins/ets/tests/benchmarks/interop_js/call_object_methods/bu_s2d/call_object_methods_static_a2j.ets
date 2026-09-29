/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

class ValueHolderStatic {
    private prop: number = 42;

    jsMethodArg0(): number {
        return this.prop;
    };
    jsMethodArg1(arg1: number): number {
        return this.prop * arg1;
    };
    jsMethodArg2(arg1: number, arg2: number): number {
        return arg1 * arg2;
    };
    jsMethodArg0V(): void {
        this.prop = 32;
    };
    jsMethodArg1V(arg1: number): void {
        this.prop = this.prop * arg1;
    };
    jsMethodArg2V(arg1: number, arg2: number): void {
        this.prop = arg1 * arg2;
    };
}

class ValueHolderStatic1 extends ValueHolderStatic {}
class ValueHolderStatic2 extends ValueHolderStatic {}
class ValueHolderStatic3 extends ValueHolderStatic {}
class ValueHolderStatic4 extends ValueHolderStatic {}


/**
 * @State
 * @Import {retHolder, ValueHolder} from "../../shared/js/libFunctions.js";
 * @Seamless '../../shared/js/libFunctions.d.ets'
 * @Tags interop, s2s, seamless, static_any
 */
class CallObjectMethodsStatic {

    /**
     * @Param 1, 2, 3, 4
     */
    public morphs: int;

    private holders : ValueHolder[] = [];

    /**
     * @Setup
     */
    public setup(): void {
        switch(this.morphs) {
          case 1:
            this.holders = [retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic1()),
                            retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic1())];
            break;
          case 2:
            this.holders = [retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic1()),
                            retHolder(new ValueHolderStatic2()), retHolder(new ValueHolderStatic2())];
            break;
          case 3:
            this.holders = [retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic2()),
                            retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic3())];
            break;
           case 4:
            this.holders = [retHolder(new ValueHolderStatic1()), retHolder(new ValueHolderStatic2()),
                            retHolder(new ValueHolderStatic3()), retHolder(new ValueHolderStatic4())];
            break;
        }
    }

    /**
     * @Benchmark
     */
    callZero(): Object {
        let result = 0.0;
        for (let i = 0; i < this.holders.length; i++) {
            result += this.holders[i].jsMethodArg0();
        }
        return result;
    };

    /**
     * @Benchmark
     */
    callShort(): Object {
        let result = 0.0;
        for (let i = 0; i < this.holders.length; i++) {
            result += this.holders[i].jsMethodArg1(24);
        }
        return result;
    };

    /**
     * @Benchmark
     */
    callRange(): Object {
        let result = 0.0;
        for (let i = 0; i < this.holders.length; i++) {
            result += this.holders[i].jsMethodArg2(42, 24);
        }
        return result;
    };

    /**
     * @Benchmark
     */
    callZeroVoid(): void {
        for (let i = 0; i < this.holders.length; i++) {
            this.holders[i].jsMethodArg0V()
        }
    };

    /**
     * @Benchmark
     */
    callShortVoid(): void {
        for (let i = 0; i < this.holders.length; i++) {
            this.holders[i].jsMethodArg1V(24)
        }
    };

    /**
     * @Benchmark
     */
    callRangeVoid(): void {
        for (let i = 0; i < this.holders.length; i++) {
            this.holders[i].jsMethodArg2V(42, 24);
        }
    };
}
