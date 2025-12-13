import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { DaliController } from './dali.controller';
import { DaliService } from './dali.service';

@Module({
    imports: [ConfigModule],
    controllers: [DaliController],
    providers: [DaliService],
})
export class DaliModule {}