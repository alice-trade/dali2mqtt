import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { DaliModule } from './dali/dali.module';

@Module({
    imports: [
        ConfigModule.forRoot({ isGlobal: true }),
        DaliModule
    ],
})
export class AppModule {}